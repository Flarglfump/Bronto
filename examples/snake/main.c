/*
 * SNAKE - a sample game for the Bronto engine.
 *
 * Eat apples to grow; don't hit the walls or yourself. Every fifth apple
 * a gold bonus appears for a few seconds - the sooner you reach it, the
 * more it is worth. The snake speeds up as it grows.
 *
 * How the code is organised (start with snake.h, then snake_game.c):
 *
 *   snake_*.c  the RULES - plain deterministic C, no engine calls, unit tested.
 *              One tick of the game is snake_step() in snake_game.c.
 *   view_*.c   DRAWING - the board and HUD (view_board.c) and the particle
 *              and pop-up effects (view_fx.c, ECS entities).
 *   sfx.c      SOUND - listens to rule events and plays synthesised sounds.
 *   main.c     GLUE (this file) - engine setup, scenes, the high score, and
 *              turning player input into snake_turn().
 *
 * Data flows one way: input -> snake_turn -> rules -> events -> effects/sound.
 * The rules record what happened as snake_events; this file forwards them
 * to the engine's message bus, where the effects and the sound pick them up.
 *
 * Controls: arrows/WASD/d-pad/left stick move, P/Esc/Start pause, Enter/A start,
 *           M mute music, F11 or Alt+Enter fullscreen.
 *
 * Flags: --autopilot   let the AI play (attract mode)
 *        --headless    no window: the AI plays a game, screenshots and a
 *                      WAV of the audio are written, exit code = success
 */
#include "view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIGHSCORE_FILE "snake_highscore.txt"

typedef struct play_data {
    snake_game game;
    view_fx fx;
    float tick_acc;             /* time banked towards the next rule tick */
    float time;
    bool over;
} play_data;

typedef struct title_data { snake_game demo; float acc; } title_data;

static struct {
    uint32_t ev_snake;          /* message type carrying a snake_event */
    br_action up, down, left, right, confirm, pause, menu;
    int best;                   /* high score, kept in HIGHSCORE_FILE */
    bool autopilot, headless;
    uint64_t games;
    /* the result of the last game, for the game-over screen */
    int last_score;
    bool new_best, last_won;
} G;

static const br_scene_desc k_title, k_play, k_pause, k_game_over;

/* ======================= high score ======================================= */

static void load_best(void) {
    size_t size;
    char *data = (char *)br_file_read_all(HIGHSCORE_FILE, &size);
    G.best = data ? atoi(data) : 0;
    br_free(data);
}

static void save_best(void) {
    char buf[32];
    if (G.headless) return;   /* demo runs never touch the player's record */
    snprintf(buf, sizeof(buf), "%d\n", G.best);
    br_file_write_all(HIGHSCORE_FILE, buf, strlen(buf));
}

/* ======================= glue: rules -> message bus ======================== */

/* Hand the rules' events to the engine bus, then clear them. */
static void forward_events(br_engine *e, snake_game *g) {
    int i;
    for (i = 0; i < g->event_count; ++i)
        br_events_post_data(&e->events, G.ev_snake, &g->events[i], sizeof(snake_event));
    snake_clear_events(g);
}

/* ======================= play scene ========================================= */

/* Board and effects are drawn by ECS render systems, in order: the board
 * (order 0) opens the shaking camera, effects (order 10) draw inside it,
 * and order 20 closes it. The HUD is drawn afterwards, outside the camera. */
static void sys_draw_board(br_world *w, float dt, void *user) {
    play_data *d = (play_data *)user;
    br_renderer *r = &BR_WORLD_ENGINE(w)->renderer;
    br_camera2d cam = view_fx_camera(&d->fx);
    BR_UNUSED(dt);
    /* under the board, so camera shake never reveals the clear colour */
    br_fill_rect(r, br_rect_make(0, HUD_H, FB_W, FB_H - HUD_H), br_rgb(31, 45, 35));
    br_begin_camera(r, &cam);
    view_draw_board(r, &d->game, d->time);
}

static void sys_end_camera(br_world *w, float dt, void *user) {
    BR_UNUSED(dt);
    BR_UNUSED(user);
    br_end_camera(&BR_WORLD_ENGINE(w)->renderer);
}

static bool play_enter(br_scene *s) {
    play_data *d = (play_data *)s->data;
    br_engine *e = s->engine;
    /* Seed from the frame number: varies with how long the player sat on
     * the title screen, yet stays reproducible for scripted runs. */
    snake_reset(&d->game, e->frame * 2654435761ull + G.games++);
    view_fx_init(&d->fx, &s->world, e->frame + 1);
    br_system_add(&s->world, "draw_board", BR_PHASE_RENDER, sys_draw_board, d, 0);
    br_system_add(&s->world, "end_camera", BR_PHASE_RENDER, sys_end_camera, d, 20);
    G.new_best = false;
    sfx_start(e);
    return true;
}

static void play_steer(play_data *d, const br_input *in) {
    if (G.autopilot) {
        if (d->game.queued == 0) snake_turn(&d->game, snake_autopilot(&d->game));
        return;
    }
    if (br_action_pressed(in, G.up))    snake_turn(&d->game, DIR_UP);
    if (br_action_pressed(in, G.down))  snake_turn(&d->game, DIR_DOWN);
    if (br_action_pressed(in, G.left))  snake_turn(&d->game, DIR_LEFT);
    if (br_action_pressed(in, G.right)) snake_turn(&d->game, DIR_RIGHT);
}

static void play_update(br_scene *s, float dt) {
    play_data *d = (play_data *)s->data;
    br_engine *e = s->engine;
    d->time += dt;
    if (d->over) return;
    if (br_action_pressed(&e->input, G.pause)) { br_scene_push(&e->scenes, &k_pause); return; }
    play_steer(d, &e->input);
}

/* The rules tick on the fixed timestep, at the snake's current speed. */
static void play_fixed(br_scene *s, float dt) {
    play_data *d = (play_data *)s->data;
    float interval;
    if (d->over) return;
    d->tick_acc += dt;
    interval = 1.0f / snake_speed(&d->game);
    while (d->tick_acc >= interval && !d->over) {
        snake_result r;
        d->tick_acc -= interval;
        if (G.autopilot && d->game.queued == 0) snake_turn(&d->game, snake_autopilot(&d->game));
        r = snake_step(&d->game);
        if (r == SNAKE_DIED || r == SNAKE_WON) d->over = true;
    }
    forward_events(s->engine, &d->game);
}

/* Rule events, now on the message bus: spawn effects; end the game. */
static bool play_event(br_scene *s, const br_event *ev) {
    play_data *d = (play_data *)s->data;
    const snake_event *e;
    if (ev->type != G.ev_snake) return false;
    e = BR_EVENT_DATA(ev, snake_event);
    view_fx_event(&d->fx, e, &d->game);
    if (e->type == SNAKE_EV_DIED || e->type == SNAKE_EV_WON) {
        G.last_score = e->score;
        G.last_won = e->type == SNAKE_EV_WON;
        if (e->score > G.best) { G.best = e->score; G.new_best = true; save_best(); }
        br_scene_push(&s->engine->scenes, &k_game_over);
    }
    return false;
}

static void play_render(br_scene *s) {
    view_draw_hud(&s->engine->renderer, &((play_data *)s->data)->game, G.best);
}

static const br_scene_desc k_play = {
    "play", sizeof(play_data), false, false,
    play_enter, NULL, NULL, NULL, play_fixed, play_update, play_render, play_event
};

/* ======================= overlays ============================================== */

static void pause_update(br_scene *s, float dt) {
    br_engine *e = s->engine;
    BR_UNUSED(dt);
    if (br_action_pressed(&e->input, G.pause) || br_action_pressed(&e->input, G.confirm)) br_scene_pop(&e->scenes);
    else if (br_action_pressed(&e->input, G.menu)) { br_scene_clear(&e->scenes); br_scene_push(&e->scenes, &k_title); }
}

static void pause_render(br_scene *s) {
    br_renderer *r = &s->engine->renderer;
    br_fill_rect(r, br_rect_make(0, HUD_H, FB_W, FB_H - HUD_H), br_rgba(0, 0, 0, 150));
    view_draw_text_centered(r, "PAUSED", 70, 3, BR_WHITE);
    view_draw_text_centered(r, "P / ESC  resume      Q  menu", 110, 1, br_rgb(190, 190, 190));
}

/* transparent: the frozen board shows through; update_below = false: frozen */
static const br_scene_desc k_pause = {
    "pause", 0, true, false, NULL, NULL, NULL, NULL, NULL, pause_update, pause_render, NULL
};

static void game_over_update(br_scene *s, float dt) {
    br_engine *e = s->engine;
    BR_UNUSED(dt);
    if (s->time < 0.6) return;   /* don't skip it by accident */
    if (br_action_pressed(&e->input, G.confirm) || (G.autopilot && s->time > 2.5)) {
        br_scene_clear(&e->scenes);
        br_scene_push(&e->scenes, &k_play);
    } else if (br_action_pressed(&e->input, G.menu) || br_action_pressed(&e->input, G.pause)) {
        br_scene_clear(&e->scenes);
        br_scene_push(&e->scenes, &k_title);
    }
}

static void game_over_render(br_scene *s) {
    br_renderer *r = &s->engine->renderer;
    char buf[48];
    float a = br_clampf((float)s->time * 3.0f, 0, 1);
    br_fill_rect(r, br_rect_make(0, HUD_H, FB_W, FB_H - HUD_H), br_rgba(10, 0, 0, (uint8_t)(150 * a)));
    view_draw_text_centered(r, G.last_won ? "YOU WIN!" : "GAME OVER", 55, 3, G.last_won ? BR_YELLOW : br_rgb(255, 90, 90));
    snprintf(buf, sizeof(buf), "SCORE %d", G.last_score);
    view_draw_text_centered(r, buf, 92, 2, BR_WHITE);
    if (G.new_best && ((int)(s->time * 4) & 1)) view_draw_text_centered(r, "NEW BEST!", 114, 1, BR_YELLOW);
    if (s->time > 0.6) view_draw_text_centered(r, "ENTER  play again      Q / ESC  menu", 140, 1, br_rgb(190, 190, 190));
}

/* update_below = true: particles keep flying under the overlay */
static const br_scene_desc k_game_over = {
    "game_over", 0, true, true, NULL, NULL, NULL, NULL, NULL, game_over_update, game_over_render, NULL
};

/* ======================= title ================================================= */

static bool title_enter(br_scene *s) {
    title_data *d = (title_data *)s->data;
    snake_reset(&d->demo, 12345);
    return true;
}

static void title_update(br_scene *s, float dt) {
    title_data *d = (title_data *)s->data;
    br_engine *e = s->engine;
    /* attract mode: the AI plays in the background, silently */
    d->acc += dt;
    while (d->acc >= 1.0f / 14.0f) {
        snake_result r;
        d->acc -= 1.0f / 14.0f;
        snake_turn(&d->demo, snake_autopilot(&d->demo));
        r = snake_step(&d->demo);
        snake_clear_events(&d->demo);
        if (r == SNAKE_DIED || r == SNAKE_WON || d->demo.body.length > 120) snake_reset(&d->demo, (uint64_t)d->demo.ticks + 1);
    }
    if (br_action_pressed(&e->input, G.confirm)) br_scene_switch(&e->scenes, &k_play);
    else if (br_key_pressed(&e->input, BR_KEY_ESCAPE)) {
        br_event q = br_event_make(BR_EVENT_QUIT);
        br_events_post(&e->events, &q);
    }
}

static void title_render(br_scene *s) {
    title_data *d = (title_data *)s->data;
    br_renderer *r = &s->engine->renderer;
    char buf[48];
    float t = (float)s->time;
    view_draw_board(r, &d->demo, t);
    br_fill_rect(r, br_rect_make(0, 0, FB_W, FB_H), br_rgba(8, 14, 10, 140));
    br_fill_rect(r, br_rect_make(0, 0, FB_W, HUD_H), br_rgb(18, 24, 22));
    br_draw_text(r, "BRONTO ENGINE SAMPLE", 6, 6, 1, br_rgb(120, 160, 130));
    view_draw_text_centered(r, "SNAKE", 38 + sinf(t * 2.0f) * 2.0f, 5, br_rgb(150, 240, 100));
    if (((int)(t * 2.0f)) & 1) view_draw_text_centered(r, "PRESS ENTER", 96, 2, BR_WHITE);
    view_draw_text_centered(r, "ARROWS / WASD / D-PAD  move", 128, 1, br_rgb(180, 200, 180));
    view_draw_text_centered(r, "P pause   M music   F11 fullscreen", 140, 1, br_rgb(180, 200, 180));
    snprintf(buf, sizeof(buf), "BEST %d", G.best);
    view_draw_text_centered(r, buf, 160, 1, BR_YELLOW);
}

static const br_scene_desc k_title = {
    "title", sizeof(title_data), false, false,
    title_enter, NULL, NULL, NULL, NULL, title_update, title_render, NULL
};

/* ======================= engine hooks ========================================== */

static bool on_hotkey(const br_event *ev, void *user) {
    br_engine *e = (br_engine *)user;
    if (ev->type != BR_EVENT_KEY_DOWN || ev->u.key.repeat) return false;
    if (ev->u.key.key == BR_KEY_F11 || (ev->u.key.key == BR_KEY_ENTER && (ev->u.key.mods & BR_MOD_ALT))) {
        br_window_toggle_fullscreen(e);
        return true;   /* Alt+Enter must not also "press Enter" in a scene */
    }
    if (ev->u.key.key == BR_KEY_M) sfx_toggle_music(e);
    return false;
}

static void bind(br_input *in, br_action a, br_key k1, br_key k2, int pad_button) {
    if (k1) br_input_bind(in, a, BR_BIND_KEY, k1, -1);
    if (k2) br_input_bind(in, a, BR_BIND_KEY, k2, -1);
    if (pad_button >= 0) br_input_bind(in, a, BR_BIND_PAD_BUTTON, pad_button, -1);
}

/* Named actions: the scenes ask "was 'up' pressed?", not which key it was. */
static void bind_actions(br_input *in) {
    G.up = br_input_action(in, "up");
    G.down = br_input_action(in, "down");
    G.left = br_input_action(in, "left");
    G.right = br_input_action(in, "right");
    G.confirm = br_input_action(in, "confirm");
    G.pause = br_input_action(in, "pause");
    G.menu = br_input_action(in, "menu");
    bind(in, G.up, BR_KEY_UP, BR_KEY_W, BR_PAD_DPAD_UP);
    bind(in, G.down, BR_KEY_DOWN, BR_KEY_S, BR_PAD_DPAD_DOWN);
    bind(in, G.left, BR_KEY_LEFT, BR_KEY_A, BR_PAD_DPAD_LEFT);
    bind(in, G.right, BR_KEY_RIGHT, BR_KEY_D, BR_PAD_DPAD_RIGHT);
    br_input_bind(in, G.up, BR_BIND_PAD_AXIS_NEG, BR_PAD_AXIS_LEFT_Y, -1);
    br_input_bind(in, G.down, BR_BIND_PAD_AXIS_POS, BR_PAD_AXIS_LEFT_Y, -1);
    br_input_bind(in, G.left, BR_BIND_PAD_AXIS_NEG, BR_PAD_AXIS_LEFT_X, -1);
    br_input_bind(in, G.right, BR_BIND_PAD_AXIS_POS, BR_PAD_AXIS_LEFT_X, -1);
    bind(in, G.confirm, BR_KEY_ENTER, BR_KEY_SPACE, BR_PAD_A);
    br_input_bind(in, G.confirm, BR_BIND_KEY, BR_KEY_KP_ENTER, -1);
    br_input_bind(in, G.confirm, BR_BIND_MOUSE_BUTTON, BR_MOUSE_LEFT, -1);
    bind(in, G.pause, BR_KEY_P, BR_KEY_ESCAPE, BR_PAD_START);
    bind(in, G.menu, BR_KEY_Q, BR_KEY_UNKNOWN, BR_PAD_BACK);
}

static bool on_init(br_engine *e) {
    G.ev_snake = br_events_register_type(&e->events, "snake.event");
    sfx_init(e, G.ev_snake);
    br_events_subscribe(&e->events, BR_EVENT_KEY_DOWN, on_hotkey, e, 500);
    bind_actions(&e->input);
    load_best();
    return true;
}

static void on_shutdown(br_engine *e) { sfx_shutdown(e); }

/* ======================= headless demo ========================================= */

static int run_headless(br_engine *e) {
    static int16_t audio[44100 * 2 * 40];
    br_sound wav;
    int frame, over_frame = -1, best_score = 0, shot_frame = -1;
    bool shot_play = false;
    br_event enter_down = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_ENTER, 0, false);
    br_event enter_up = br_event_key(BR_EVENT_KEY_UP, BR_KEY_ENTER, 0, false);
    br_headless_capture_audio(audio, 44100 * 40);
    for (frame = 0; frame < 60 * 40 && e->running; ++frame) {
        br_scene *top;
        if (frame == 45) { br_headless_inject(&enter_down); br_headless_inject(&enter_up); }
        br_engine_frame(e);
        top = br_scenes_top(&e->scenes);
        if (frame == 30) br_image_save_bmp(&e->framebuffer, "snake_title.bmp");
        if (top && top->desc == &k_play) {
            play_data *d = (play_data *)top->data;
            if (d->game.score > best_score) best_score = d->game.score;
            /* a frame with effects in flight: just after a bonus pickup */
            if (shot_frame < 0 && d->game.apples >= 10 && br_component_count(&top->world, d->fx.particle) > 25)
                shot_frame = frame + 8;
            if (!shot_play && frame == shot_frame) {
                br_image_save_bmp(&e->framebuffer, "snake_play.bmp");
                shot_play = true;
            }
        }
        if (top && top->desc == &k_game_over && over_frame < 0) over_frame = frame;
        if (over_frame >= 0 && frame == over_frame + 50) {
            br_image_save_bmp(&e->framebuffer, "snake_game_over.bmp");
            break;
        }
        /* Long enough: crash the snake into the wall to show game over. */
        if (frame == 60 * 30 && top && top->desc == &k_play) G.autopilot = false;
    }
    if (!shot_play) br_image_save_bmp(&e->framebuffer, "snake_play.bmp");
    wav.samples = audio;
    wav.frames = br_headless_captured_frames();
    wav.channels = 2;
    wav.sample_rate = 44100;
    br_sound_save_wav(&wav, "snake_audio.wav");
    printf("snake (headless): %d frames, best score %d, game over %s\n"
           "wrote snake_title.bmp, snake_play.bmp, snake_game_over.bmp, snake_audio.wav\n",
           frame, best_score, over_frame >= 0 ? "reached" : "not reached");
    return best_score >= 100 && over_frame >= 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config cfg = br_engine_default_config();
    int i, rc = 0;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) G.headless = G.autopilot = true;
        else if (strcmp(argv[i], "--autopilot") == 0) G.autopilot = true;
    }
    cfg.title = "Bronto Snake";
    cfg.width = FB_W;
    cfg.height = FB_H;
    cfg.window_scale = 4;
    cfg.scale_mode = BR_SCALE_INTEGER;        /* crisp, evenly sized pixels */
    cfg.clear_color = br_rgb(10, 14, 12);
    cfg.initial_scene = &k_title;
    cfg.on_init = on_init;
    cfg.on_shutdown = on_shutdown;
    if (G.headless) cfg.platform = br_platform_headless();
    if (!br_engine_init(e, &cfg)) { br_free(e); return 1; }

    if (G.headless) rc = run_headless(e);
    else while (br_engine_frame(e)) { /* the scenes do the rest */ }

    br_engine_shutdown(e);
    br_free(e);
    return rc;
}
