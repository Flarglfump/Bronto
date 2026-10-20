/*
 * POP DEFENSE - a balloon-popping tower defense sample for the Bronto engine.
 *
 * Build towers beside the path; balloons float along it in 30 rounds and
 * every layer you pop earns $1. Upgrade towers down two paths, and survive
 * all 30 rounds to win.
 *
 * How the code is organised (start with td.h, then td_game.c):
 *
 *   td_*.c     the RULES - plain deterministic C, no engine calls, unit tested.
 *              The game loop is td_game_step() in td_game.c.
 *   view_*.c   DRAWING - reads the rules' state and paints it.
 *   sfx.c      SOUND - listens to rule events and plays synthesised sounds.
 *   main.c     GLUE (this file) - engine setup, scenes, and turning player
 *              input into rule commands (td_place_tower, td_upgrade_tower...).
 *
 * Data flows one way: input -> commands -> rules -> events -> effects/sound.
 * The rules record what happened as td_events; this file forwards them to
 * the engine's message bus, where the effects (ECS entities) and the sound
 * module pick them up.
 *
 * Controls
 *   mouse        click a tower in the shop, then the grass to build it;
 *                click a tower to select it (upgrade / sell / targeting)
 *   1-5          pick a tower        Shift+click  keep building
 *   Space        start the round / change speed (x1, x2, x3)
 *   , .          buy the next upgrade on path 1 / path 2
 *   Tab          change targeting    Backspace / Delete  sell
 *   Esc / right  cancel / deselect;  Esc or P pauses
 *   M  music     F11 / Alt+Enter  fullscreen
 *   gamepad      left stick / d-pad move the cursor, A click, B cancel,
 *                X play / speed, Y targeting, LB / RB pick a tower, Start pause
 *
 * Flags: --autopilot  watch the scripted player
 *        --headless   no window: the scripted player plays all 30 rounds at
 *                     triple speed, writes screenshots and a WAV of the
 *                     first 45 seconds of audio; exit code 0 on victory
 */
#include "view.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct play_data {
    td_game game;
    td_autopilot pilot;
    view_fx fx;
    int selected;               /* tower index, -1 = none */
    int placing;                /* tower type, -1 = none */
    int speed;                  /* rule steps per fixed update: 1..3 */
    bool auto_start;
    const char *message;
    float message_time;
    br_vec2 cursor;
    bool pad_cursor;            /* last moved with a gamepad: draw our own cursor */
    float time;
} play_data;

typedef struct title_data { td_game game; td_autopilot pilot; } title_data;

static struct {
    uint32_t ev_td;             /* message type carrying a td_event */
    bool autopilot, headless;
    /* the result of the last game, for the end screen */
    bool won;
    int round, pops, lives;
} G;

static const br_scene_desc k_title, k_play, k_pause, k_end;

/* ======================= glue: rules -> message bus ======================== */

/* Hand the rules' events to the engine bus, then clear them. */
static void forward_events(br_engine *e, td_game *g) {
    int i;
    for (i = 0; i < g->event_count; ++i)
        br_events_post_data(&e->events, G.ev_td, &g->events[i], sizeof(td_event));
    td_game_clear_events(g);
}

/* ======================= play scene ========================================= */

static void notify(play_data *d, br_engine *e, td_result r) {
    d->message = td_result_text(r);
    d->message_time = 1.6f;
    sfx_ui(e, SFX_ERROR);
}

static ui_state make_ui(const play_data *d) {
    ui_state u;
    u.game = &d->game;
    u.selected = d->selected;
    u.placing = d->placing;
    u.speed = d->speed;
    u.auto_start = d->auto_start;
    u.message = d->message;
    u.message_time = d->message_time;
    u.cursor = d->cursor;
    u.time = d->time;
    return u;
}

static void begin_placing(play_data *d, br_engine *e, int type) {
    d->placing = d->placing == type ? -1 : type;
    d->selected = -1;
    sfx_ui(e, SFX_CLICK);
}

/* The play / speed button. */
static void press_go(play_data *d, br_engine *e) {
    if (d->game.phase == TD_PHASE_BUILD) td_start_round(&d->game);
    else if (!td_game_over(&d->game)) { d->speed = d->speed % 3 + 1; sfx_ui(e, SFX_CLICK); }
}

static void upgrade(play_data *d, br_engine *e, int path) {
    td_result r = td_upgrade_tower(&d->game, d->selected, path);
    if (r != TD_OK && r != TD_ERR_NO_TOWER) notify(d, e, r);
}

static void sell(play_data *d) {
    if (d->selected < 0) return;
    td_sell_tower(&d->game, d->selected);
    d->selected = -1;
}

static void click_panel(play_data *d, br_engine *e, ui_widget w) {
    if (w >= UI_SHOP && w < UI_SHOP + TD_TOWER_TYPE_COUNT) begin_placing(d, e, w - UI_SHOP);
    else if (w == UI_TARGET) { td_cycle_targeting(&d->game, d->selected, 1); sfx_ui(e, SFX_CLICK); }
    else if (w == UI_UPGRADE || w == UI_UPGRADE + 1) upgrade(d, e, w - UI_UPGRADE);
    else if (w == UI_SELL) sell(d);
    else if (w == UI_DESELECT) d->selected = -1;
    else if (w == UI_GO) press_go(d, e);
    else if (w == UI_AUTO) { d->auto_start = !d->auto_start; sfx_ui(e, SFX_CLICK); }
}

static void click_map(play_data *d, br_engine *e, bool keep_placing) {
    if (d->placing >= 0) {
        td_result r = td_place_tower(&d->game, (td_tower_type)d->placing, d->cursor);
        if (r != TD_OK) { notify(d, e, r); return; }
        if (!keep_placing) d->placing = -1;
        return;
    }
    d->selected = td_tower_at(&d->game, d->cursor);
    if (d->selected >= 0) sfx_ui(e, SFX_CLICK);
}

static void cancel(play_data *d) {
    d->placing = -1;
    d->selected = -1;
}

/* Mouse, keyboard and gamepad move one cursor and press the same buttons. */
static void play_input(br_scene *s, float dt) {
    play_data *d = (play_data *)s->data;
    br_engine *e = s->engine;
    br_input *in = &e->input;
    int pad, i;
    bool click = br_mouse_pressed(in, BR_MOUSE_LEFT), back = br_mouse_pressed(in, BR_MOUSE_RIGHT);
    bool go = br_key_pressed(in, BR_KEY_SPACE), target = br_key_pressed(in, BR_KEY_TAB);
    int shop_step = 0;

    if (in->mouse_dx != 0 || in->mouse_dy != 0) { d->cursor = br_v2(in->mouse_x, in->mouse_y); d->pad_cursor = false; }
    for (pad = 0; pad < BR_MAX_GAMEPADS; ++pad) {
        float x, y;
        if (!br_pad_connected(in, pad)) continue;
        x = br_pad_axis(in, pad, BR_PAD_AXIS_LEFT_X) + (float)br_pad_down(in, pad, BR_PAD_DPAD_RIGHT) - (float)br_pad_down(in, pad, BR_PAD_DPAD_LEFT);
        y = br_pad_axis(in, pad, BR_PAD_AXIS_LEFT_Y) + (float)br_pad_down(in, pad, BR_PAD_DPAD_DOWN) - (float)br_pad_down(in, pad, BR_PAD_DPAD_UP);
        if (x != 0 || y != 0) {
            d->cursor = br_v2(br_clampf(d->cursor.x + x * 280.0f * dt, 0, FB_W - 1), br_clampf(d->cursor.y + y * 280.0f * dt, 0, FB_H - 1));
            d->pad_cursor = true;
        }
        click |= br_pad_pressed(in, pad, BR_PAD_A);
        back |= br_pad_pressed(in, pad, BR_PAD_B);
        go |= br_pad_pressed(in, pad, BR_PAD_X);
        target |= br_pad_pressed(in, pad, BR_PAD_Y);
        shop_step += br_pad_pressed(in, pad, BR_PAD_RIGHT_BUMPER) - br_pad_pressed(in, pad, BR_PAD_LEFT_BUMPER);
    }

    for (i = 0; i < TD_TOWER_TYPE_COUNT; ++i)
        if (br_key_pressed(in, (br_key)(BR_KEY_1 + i))) begin_placing(d, e, i);
    if (shop_step) begin_placing(d, e, ((d->placing < 0 ? (shop_step > 0 ? -1 : 0) : d->placing) + shop_step + TD_TOWER_TYPE_COUNT) % TD_TOWER_TYPE_COUNT);
    if (go) press_go(d, e);
    if (target && d->selected >= 0) { td_cycle_targeting(&d->game, d->selected, 1); sfx_ui(e, SFX_CLICK); }
    if (br_key_pressed(in, BR_KEY_COMMA)) upgrade(d, e, 0);
    if (br_key_pressed(in, BR_KEY_PERIOD)) upgrade(d, e, 1);
    if (br_key_pressed(in, BR_KEY_BACKSPACE) || br_key_pressed(in, BR_KEY_DELETE)) sell(d);
    if (back) cancel(d);

    if (click) {
        if (d->cursor.x >= PANEL_X) {
            ui_state u = make_ui(d);
            click_panel(d, e, view_ui_hit(&u, d->cursor));
        } else {
            click_map(d, e, (in->mods & BR_MOD_SHIFT) != 0);
        }
    }
}

static void sys_draw_world(br_world *w, float dt, void *user);

static bool play_enter(br_scene *s) {
    play_data *d = (play_data *)s->data;
    td_game_init(&d->game);
    td_autopilot_init(&d->pilot);
    view_fx_init(&d->fx, &s->world);
    d->selected = d->placing = -1;
    d->speed = G.headless ? 3 : 1;
    d->cursor = br_v2(TD_MAP_W / 2.0f, TD_MAP_H / 2.0f);
    br_system_add(&s->world, "draw_world", BR_PHASE_RENDER, sys_draw_world, d, 0);
    return true;
}

static void play_update(br_scene *s, float dt) {
    play_data *d = (play_data *)s->data;
    br_engine *e = s->engine;
    br_input *in = &e->input;
    bool esc = br_key_pressed(in, BR_KEY_ESCAPE);
    int pad;
    d->time += dt;
    if (d->message_time > 0) d->message_time -= dt;

    for (pad = 0; pad < BR_MAX_GAMEPADS; ++pad) esc |= br_pad_pressed(in, pad, BR_PAD_START);
    if (br_key_pressed(in, BR_KEY_P) || (esc && d->placing < 0 && d->selected < 0)) {
        br_scene_push(&e->scenes, &k_pause);
        return;
    }
    if (esc) cancel(d);
    if (!G.autopilot) play_input(s, dt);
    if (d->selected >= d->game.tower_count) d->selected = -1;
    forward_events(e, &d->game);        /* placing / upgrading / selling make events too */
}

/* The rules advance on the engine's fixed 60 Hz step; fast-forward simply
 * runs more rule steps per tick, so x3 plays out exactly like x1. */
static void play_fixed(br_scene *s, float dt) {
    play_data *d = (play_data *)s->data;
    td_game *g = &d->game;
    int i;
    BR_UNUSED(dt);
    for (i = 0; i < d->speed; ++i) {
        if (G.autopilot) td_autopilot_play(&d->pilot, g);
        else if (d->auto_start && g->phase == TD_PHASE_BUILD && g->round > 0) td_start_round(g);
        td_game_step(g);
    }
    forward_events(s->engine, g);
}

/* Rule events, now on the message bus: spawn effects; end the game. */
static bool play_event(br_scene *s, const br_event *ev) {
    play_data *d = (play_data *)s->data;
    if (ev->type == G.ev_td) {
        const td_event *t = BR_EVENT_DATA(ev, td_event);
        view_fx_event(&d->fx, t);
        if (t->type == TD_EV_DEFEAT || t->type == TD_EV_VICTORY) {
            G.won = t->type == TD_EV_VICTORY;
            G.round = d->game.round;
            G.pops = d->game.pops;
            G.lives = d->game.lives;
            d->placing = d->selected = -1;
            view_fx_clear(&d->fx);
            br_scene_push(&s->engine->scenes, &k_end);
        }
    }
    return false;
}

/* World first (ECS render system, order 0), then effects (view_fx, order 10),
 * then the scene callback draws the build preview, the panel and the cursor. */
static void sys_draw_world(br_world *w, float dt, void *user) {
    play_data *d = (play_data *)user;
    BR_UNUSED(dt);
    view_draw_world(&BR_WORLD_ENGINE(w)->renderer, &d->game, d->time, d->selected);
}

static void draw_placement_preview(br_renderer *r, const play_data *d) {
    const td_tower_def *def;
    td_result ok;
    br_color c;
    if (d->placing < 0 || d->cursor.x >= PANEL_X) return;
    def = td_tower_info((td_tower_type)d->placing);
    ok = td_can_place(&d->game, (td_tower_type)d->placing, d->cursor);
    c = ok == TD_OK ? br_rgba(255, 255, 255, 190) : ok == TD_ERR_CASH ? br_rgba(255, 180, 60, 190) : br_rgba(255, 70, 70, 200);
    if (def->base.attack != TD_ATK_HITSCAN) view_draw_range(r, d->cursor, def->base.range, c);
    view_draw_tower(r, (td_tower_type)d->placing, d->cursor, br_v2(0, -1), NULL, 1.0f);
    if (ok == TD_ERR_BLOCKED) {
        br_draw_line(r, br_v2(d->cursor.x - 6, d->cursor.y - 6), br_v2(d->cursor.x + 6, d->cursor.y + 6), br_rgb(255, 60, 60));
        br_draw_line(r, br_v2(d->cursor.x + 6, d->cursor.y - 6), br_v2(d->cursor.x - 6, d->cursor.y + 6), br_rgb(255, 60, 60));
    }
}

static void draw_pad_cursor(br_renderer *r, br_vec2 p) {
    br_fill_triangle(r, p, br_v2(p.x, p.y + 11), br_v2(p.x + 8, p.y + 8), BR_BLACK);
    br_fill_triangle(r, br_v2(p.x + 1, p.y + 2), br_v2(p.x + 1, p.y + 9), br_v2(p.x + 6, p.y + 7.5f), BR_WHITE);
}

static void play_render(br_scene *s) {
    play_data *d = (play_data *)s->data;
    br_renderer *r = &s->engine->renderer;
    ui_state u = make_ui(d);
    draw_placement_preview(r, d);
    view_draw_ui(r, &u);
    if (d->pad_cursor) draw_pad_cursor(r, d->cursor);
    if (G.autopilot && !G.headless) br_draw_text(r, "AUTOPILOT", 6, 6, 1, br_rgba(255, 255, 255, 160));
}

static const br_scene_desc k_play = {
    "play", sizeof(play_data), false, false,
    play_enter, NULL, NULL, NULL, play_fixed, play_update, play_render, play_event
};

/* ======================= overlays ============================================= */

static void dim_map(br_renderer *r, uint8_t alpha) { br_fill_rect(r, br_rect_make(0, 0, TD_MAP_W, FB_H), br_rgba(10, 14, 24, alpha)); }

static void pause_update(br_scene *s, float dt) {
    br_engine *e = s->engine;
    br_input *in = &e->input;
    int pad;
    bool resume = br_key_pressed(in, BR_KEY_P) || br_key_pressed(in, BR_KEY_ESCAPE) || br_key_pressed(in, BR_KEY_ENTER);
    bool quit = br_key_pressed(in, BR_KEY_Q);
    BR_UNUSED(dt);
    for (pad = 0; pad < BR_MAX_GAMEPADS; ++pad) {
        resume |= br_pad_pressed(in, pad, BR_PAD_START) || br_pad_pressed(in, pad, BR_PAD_A);
        quit |= br_pad_pressed(in, pad, BR_PAD_BACK);
    }
    if (resume) br_scene_pop(&e->scenes);
    else if (quit) { br_scene_clear(&e->scenes); br_scene_push(&e->scenes, &k_title); }
}

static void pause_render(br_scene *s) {
    br_renderer *r = &s->engine->renderer;
    dim_map(r, 150);
    view_draw_text_centered(r, "PAUSED", TD_MAP_W / 2.0f, 130, 3, BR_WHITE);
    view_draw_text_centered(r, "P / ESC  resume      Q  quit to title", TD_MAP_W / 2.0f, 175, 1, br_rgb(190, 200, 215));
}

/* transparent: the frozen game shows through; update_below = false: it stays frozen */
static const br_scene_desc k_pause = {
    "pause", 0, true, false, NULL, NULL, NULL, NULL, NULL, pause_update, pause_render, NULL
};

static void end_update(br_scene *s, float dt) {
    br_engine *e = s->engine;
    br_input *in = &e->input;
    int pad;
    bool again = br_key_pressed(in, BR_KEY_ENTER) || br_mouse_pressed(in, BR_MOUSE_LEFT) || (G.autopilot && !G.headless && s->time > 5);
    bool quit = br_key_pressed(in, BR_KEY_Q) || br_key_pressed(in, BR_KEY_ESCAPE);
    BR_UNUSED(dt);
    if (s->time < 1.0) return;    /* don't skip it by accident */
    for (pad = 0; pad < BR_MAX_GAMEPADS; ++pad) {
        again |= br_pad_pressed(in, pad, BR_PAD_A);
        quit |= br_pad_pressed(in, pad, BR_PAD_B);
    }
    if (again) { br_scene_clear(&e->scenes); br_scene_push(&e->scenes, &k_play); }
    else if (quit) { br_scene_clear(&e->scenes); br_scene_push(&e->scenes, &k_title); }
}

static void end_render(br_scene *s) {
    br_renderer *r = &s->engine->renderer;
    char buf[48];
    float cx = TD_MAP_W / 2.0f, a = br_clampf((float)s->time * 2.0f, 0, 1);
    dim_map(r, (uint8_t)(170 * a));
    view_draw_text_centered(r, G.won ? "VICTORY!" : "GAME OVER", cx, 90, 4, G.won ? br_rgb(255, 225, 100) : br_rgb(255, 100, 100));
    snprintf(buf, sizeof(buf), G.won ? "All %d rounds survived" : "The balloons got through on round %d", G.won ? td_round_count() : G.round);
    view_draw_text_centered(r, buf, cx, 140, 1, BR_WHITE);
    snprintf(buf, sizeof(buf), "Balloons popped  %d", G.pops);
    view_draw_text_centered(r, buf, cx, 160, 1, br_rgb(200, 210, 225));
    if (G.won) {
        snprintf(buf, sizeof(buf), "Lives left  %d", G.lives);
        view_draw_text_centered(r, buf, cx, 174, 1, br_rgb(200, 210, 225));
    }
    if (s->time > 1.0) view_draw_text_centered(r, "ENTER  play again      Q  title", cx, 210, 1, br_rgb(190, 200, 215));
}

/* update_below: effects keep playing under the overlay (the rules have stopped) */
static const br_scene_desc k_end = {
    "end", 0, true, true, NULL, NULL, NULL, NULL, NULL, end_update, end_render, NULL
};

/* ======================= title ================================================== */

static bool title_enter(br_scene *s) {
    title_data *d = (title_data *)s->data;
    td_game_init(&d->game);
    td_autopilot_init(&d->pilot);
    return true;
}

static void title_fixed(br_scene *s, float dt) {     /* attract mode: the autopilot plays */
    title_data *d = (title_data *)s->data;
    int i;
    BR_UNUSED(dt);
    for (i = 0; i < 2; ++i) {
        td_autopilot_play(&d->pilot, &d->game);
        td_game_step(&d->game);
    }
    td_game_clear_events(&d->game);                   /* the attract game is silent */
    if (td_game_over(&d->game) || d->game.round > 14) title_enter(s);
}

static void title_update(br_scene *s, float dt) {
    br_engine *e = s->engine;
    br_input *in = &e->input;
    int pad;
    bool start = br_key_pressed(in, BR_KEY_ENTER) || br_key_pressed(in, BR_KEY_SPACE) || br_mouse_pressed(in, BR_MOUSE_LEFT);
    BR_UNUSED(dt);
    for (pad = 0; pad < BR_MAX_GAMEPADS; ++pad) start |= br_pad_pressed(in, pad, BR_PAD_A) || br_pad_pressed(in, pad, BR_PAD_START);
    if (start) br_scene_switch(&e->scenes, &k_play);
    else if (br_key_pressed(in, BR_KEY_ESCAPE)) {
        br_event q = br_event_make(BR_EVENT_QUIT);
        br_events_post(&e->events, &q);
    }
}

static void title_render(br_scene *s) {
    title_data *d = (title_data *)s->data;
    br_renderer *r = &s->engine->renderer;
    float t = (float)s->time, cx = TD_MAP_W / 2.0f;
    static const char *help[] = {
        "Build towers beside the path.",
        "Every layer popped earns $1.",
        "Balloons that get through cost lives.",
        "",
        "1-5  pick a tower      , .  upgrade",
        "Space  play / speed    Tab  targeting",
        "Esc  cancel / pause    M  music",
    };
    int i;
    view_draw_world(r, &d->game, t, -1);
    br_fill_rect(r, br_rect_make(0, 0, TD_MAP_W, FB_H), br_rgba(12, 18, 30, 150));
    view_draw_text_centered(r, "POP", cx, 34 + sinf(t * 2.0f) * 2.0f, 5, br_rgb(255, 225, 100));
    view_draw_text_centered(r, "DEFENSE", cx, 78 + sinf(t * 2.0f + 0.6f) * 2.0f, 5, br_rgb(255, 225, 100));
    view_draw_text_centered(r, "a tower defense sample for the Bronto engine", cx, 126, 1, br_rgb(170, 190, 215));
    view_draw_balloon(r, TD_RED, false, br_v2(cx - 100, 50 + sinf(t * 2.3f) * 4), 2.0f);
    view_draw_balloon(r, TD_BLUE, false, br_v2(cx + 100, 50 + sinf(t * 2.0f + 1) * 4), 2.0f);
    view_draw_balloon(r, TD_RAINBOW, false, br_v2(cx + 172, 92 + sinf(t * 2.6f + 2) * 4), 1.5f);
    view_draw_balloon(r, TD_GREEN, false, br_v2(cx - 172, 92 + sinf(t * 1.8f + 3) * 4), 1.5f);
    if (((int)(t * 2.0f)) & 1) view_draw_text_centered(r, "CLICK OR PRESS ENTER", cx, 160, 2, BR_WHITE);
    for (i = 0; i < (int)BR_ARRAY_COUNT(help); ++i)
        view_draw_text_centered(r, help[i], cx, 204 + (float)i * 13, 1, br_rgb(205, 215, 230));
    view_draw_text_centered(r, "30 rounds on Meadow Lanes", cx, 312, 1, br_rgb(150, 170, 200));
    view_draw_balloon_legend(r);
}

static const br_scene_desc k_title = {
    "title", sizeof(title_data), false, false,
    title_enter, NULL, NULL, NULL, title_fixed, title_update, title_render, NULL
};

/* ======================= engine hooks =========================================== */

static bool on_hotkey(const br_event *ev, void *user) {
    br_engine *e = (br_engine *)user;
    if (ev->type != BR_EVENT_KEY_DOWN || ev->u.key.repeat) return false;
    if (ev->u.key.key == BR_KEY_F11 || (ev->u.key.key == BR_KEY_ENTER && (ev->u.key.mods & BR_MOD_ALT))) {
        br_window_toggle_fullscreen(e);
        return true;
    }
    if (ev->u.key.key == BR_KEY_M) sfx_toggle_music(e);
    return false;
}

static bool on_init(br_engine *e) {
    td_map map;
    td_map_build(&map);
    view_world_init(&map);
    G.ev_td = br_events_register_type(&e->events, "td.event");
    sfx_init(e, G.ev_td);
    br_events_subscribe(&e->events, BR_EVENT_KEY_DOWN, on_hotkey, e, 500);
    return true;
}

static void on_shutdown(br_engine *e) {
    sfx_shutdown(e);
    view_world_shutdown();
}

/* ======================= headless demo ============================================ */

static int run_headless(br_engine *e) {
    static int16_t audio[44100 * 2 * 45];
    br_event enter_down = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_ENTER, 0, false);
    br_event enter_up = br_event_key(BR_EVENT_KEY_UP, BR_KEY_ENTER, 0, false);
    bool shot_play = false, shot_early = false;
    int frame, end_frame = -1;
    br_sound wav;
    br_headless_capture_audio(audio, 44100 * 45);
    for (frame = 0; frame < 30000 && e->running; ++frame) {
        br_scene *top;
        if (frame == 45) { br_headless_inject(&enter_down); br_headless_inject(&enter_up); }
        br_engine_frame(e);
        top = br_scenes_top(&e->scenes);
        if (frame == 30) br_image_save_bmp(&e->framebuffer, "td_title.bmp");
        if (top && top->desc == &k_play) {
            play_data *d = (play_data *)top->data;
            if (!shot_early && d->game.round >= 5 && d->game.balloon_count > 12) {
                d->placing = TD_ICE;                             /* show the build preview */
                d->cursor = br_v2(140, 210);
                br_engine_frame(e);
                br_image_save_bmp(&e->framebuffer, "td_early.bmp");
                d->placing = -1;
                shot_early = true;
            }
            if (!shot_play && d->game.round >= 27 && d->game.balloon_count > 60) {
                d->selected = 2;                                 /* show the upgrade panel */
                br_engine_frame(e);
                br_image_save_bmp(&e->framebuffer, "td_play.bmp");
                d->selected = -1;
                shot_play = true;
            }
        }
        if (top && top->desc == &k_end && end_frame < 0) end_frame = frame;
        if (end_frame >= 0 && frame == end_frame + 90) {
            br_image_save_bmp(&e->framebuffer, "td_end.bmp");
            break;
        }
    }
    wav.samples = audio;
    wav.frames = br_headless_captured_frames();
    wav.channels = 2;
    wav.sample_rate = 44100;
    br_sound_save_wav(&wav, "td_audio.wav");
    printf("towerdefense (headless): %d frames, %s on round %d, %d lives left, %d balloons popped\n"
           "wrote td_title.bmp, td_early.bmp, td_play.bmp, td_end.bmp, td_audio.wav\n",
           frame, end_frame < 0 ? "unfinished" : G.won ? "victory" : "defeat", G.round, G.lives, G.pops);
    return end_frame >= 0 && G.won ? 0 : 1;
}

int main(int argc, char **argv) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config cfg = br_engine_default_config();
    int i, rc = 0;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) G.headless = G.autopilot = true;
        else if (strcmp(argv[i], "--autopilot") == 0) G.autopilot = true;
    }
    cfg.title = "Bronto - Pop Defense";
    cfg.width = FB_W;
    cfg.height = FB_H;
    cfg.window_scale = 2;
    cfg.scale_mode = BR_SCALE_FIT;
    cfg.clear_color = br_rgb(12, 16, 24);
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
