/*
 * Bronto example: "bounce".
 *
 * Shows every core system working together:
 *   - scenes: title -> game, with a transparent pause overlay pushed on top
 *   - ECS: Transform / Velocity / Sprite components, move + bounce + draw systems
 *   - input: named actions bound to keys and gamepad
 *   - events: a custom "ball_bounced" message posted by a system and
 *             consumed by a subscriber that plays a sound
 *   - rendering: sprites, primitives, camera, text
 *   - audio: generated sound effects
 *
 * Run normally it opens a window: ENTER starts, click spawns a ball,
 * P/Esc pauses, F11 toggles fullscreen. With --headless it runs without a
 * window, feeds itself scripted input and writes screenshots + audio.
 */
#include "bronto/bronto.h"
#include <stdio.h>
#include <string.h>

/* ---- components ------------------------------------------------------- */
typedef struct Transform { br_vec2 pos; float rotation; float spin; } Transform;
typedef struct Velocity  { br_vec2 v; } Velocity;
typedef struct Sprite    { const br_image *image; br_color tint; float scale; } Sprite;

/* ---- shared game state -------------------------------------------------- */
typedef struct game {
    br_image ball;
    br_sound bounce_sfx, start_sfx;
    uint32_t ev_bounced;
    br_action act_start, act_pause, act_spawn, act_quit;
    int bounces;
} game;
static game G;

typedef struct bounce_msg { br_entity who; float speed; } bounce_msg;

static void make_ball_image(br_image *img) {
    int x, y;
    br_image_init(img, 16, 16);
    for (y = 0; y < 16; ++y)
        for (x = 0; x < 16; ++x) {
            float dx = (float)x - 7.5f, dy = (float)y - 7.5f, d = sqrtf(dx * dx + dy * dy);
            if (d < 7.5f) {
                uint8_t shade = (uint8_t)(255 - (int)(d * 14));
                br_image_set(img, x, y, ((x / 4 + y / 4) & 1) ? br_rgb(shade, shade, 255) : br_rgb(255, shade / 2, 60));
            }
        }
}

/* ---- systems --------------------------------------------------------------- */
typedef struct game_scene { br_component TRANSFORM, VELOCITY, SPRITE; br_rng rng; } game_scene;

static void sys_move(br_world *w, float dt, void *u) {
    game_scene *gs = (game_scene *)u;
    br_engine *e = BR_WORLD_ENGINE(w);
    br_component q[2];
    br_query it;
    float W = (float)e->config.width, H = (float)e->config.height;
    q[0] = gs->TRANSFORM; q[1] = gs->VELOCITY;
    it = br_query_make(w, q, 2);
    while (br_query_next(&it)) {
        Transform *t = BR_QUERY_GET(&it, 0, Transform);
        Velocity *v = BR_QUERY_GET(&it, 1, Velocity);
        bool bounced = false;
        v->v.y += 300.0f * dt;                       /* gravity */
        t->pos = br_v2_add(t->pos, br_v2_scale(v->v, dt));
        t->rotation += t->spin * dt;
        if (t->pos.x < 8)     { t->pos.x = 8;     v->v.x = -v->v.x; bounced = true; }
        if (t->pos.x > W - 8) { t->pos.x = W - 8; v->v.x = -v->v.x; bounced = true; }
        if (t->pos.y > H - 8) { t->pos.y = H - 8; v->v.y = -v->v.y * 0.9f; bounced = true; }
        if (bounced) {
            bounce_msg m;
            m.who = it.entity;
            m.speed = br_v2_len(v->v);
            br_events_post_data(&e->events, G.ev_bounced, &m, sizeof(m));
        }
    }
}

static void sys_draw(br_world *w, float dt, void *u) {
    game_scene *gs = (game_scene *)u;
    br_engine *e = BR_WORLD_ENGINE(w);
    br_component q[2];
    br_query it;
    BR_UNUSED(dt);
    q[0] = gs->TRANSFORM; q[1] = gs->SPRITE;
    it = br_query_make(w, q, 2);
    while (br_query_next(&it)) {
        Transform *t = BR_QUERY_GET(&it, 0, Transform);
        Sprite *s = BR_QUERY_GET(&it, 1, Sprite);
        br_draw_sprite(&e->renderer, s->image, NULL, t->pos, br_v2(8, 8),
                       br_v2(s->scale, s->scale), t->rotation, s->tint);
    }
}

static void spawn_ball(br_scene *s, br_vec2 pos) {
    game_scene *gs = (game_scene *)s->data;
    br_entity e = br_entity_create(&s->world);
    Transform t;
    Velocity v;
    Sprite sp;
    /* Each RNG call is its own statement: C leaves the evaluation order of
     * function arguments unspecified, so br_v2(rng(), rng()) would give
     * different results on different compilers/CPUs and break determinism. */
    t.pos = pos; t.rotation = 0; t.spin = br_rng_range(&gs->rng, -4, 4);
    v.v.x = br_rng_range(&gs->rng, -120, 120);
    v.v.y = br_rng_range(&gs->rng, -200, -50);
    sp.image = &G.ball; sp.scale = br_rng_range(&gs->rng, 0.75f, 1.5f);
    sp.tint = BR_WHITE;
    sp.tint.r = (uint8_t)br_rng_int(&gs->rng, 150, 255);
    sp.tint.g = (uint8_t)br_rng_int(&gs->rng, 150, 255);
    br_add(&s->world, e, gs->TRANSFORM, &t);
    br_add(&s->world, e, gs->VELOCITY, &v);
    br_add(&s->world, e, gs->SPRITE, &sp);
}

/* ---- scenes ------------------------------------------------------------------ */
static const br_scene_desc k_pause_scene;

static bool game_enter(br_scene *s) {
    game_scene *gs = (game_scene *)s->data;
    int i;
    gs->TRANSFORM = BR_COMPONENT(&s->world, Transform);
    gs->VELOCITY  = BR_COMPONENT(&s->world, Velocity);
    gs->SPRITE    = BR_COMPONENT(&s->world, Sprite);
    br_rng_seed(&gs->rng, 7);
    br_system_add(&s->world, "move", BR_PHASE_FIXED_UPDATE, sys_move, gs, 0);
    br_system_add(&s->world, "draw", BR_PHASE_RENDER, sys_draw, gs, 0);
    for (i = 0; i < 12; ++i) spawn_ball(s, br_v2(40.0f + (float)i * 20.0f, 40));
    return true;
}

static void game_update(br_scene *s, float dt) {
    br_input *in = &s->engine->input;
    BR_UNUSED(dt);
    if (br_action_pressed(in, G.act_spawn)) spawn_ball(s, br_v2(in->mouse_x, in->mouse_y));
    if (br_action_pressed(in, G.act_pause)) br_scene_push(&s->engine->scenes, &k_pause_scene);
}

static void game_render(br_scene *s) {
    br_renderer *r = &s->engine->renderer;
    br_draw_textf(r, 4, 4, 1, BR_WHITE, "balls:%u bounces:%d fps:%.0f",
                  br_world_entity_count(&s->world), G.bounces, s->engine->fps);
}

static const br_scene_desc k_game_scene = {
    "game", sizeof(game_scene), false, false,
    game_enter, NULL, NULL, NULL, NULL, game_update, game_render, NULL
};

static void pause_update(br_scene *s, float dt) {
    BR_UNUSED(dt);
    if (br_action_pressed(&s->engine->input, G.act_pause)) br_scene_pop(&s->engine->scenes);
}
static void pause_render(br_scene *s) {
    br_renderer *r = &s->engine->renderer;
    br_vec2 sz = br_measure_text("PAUSED", 2);
    br_fill_rect(r, br_rect_make(0, 0, (float)s->engine->config.width, (float)s->engine->config.height),
                 br_rgba(0, 0, 0, 140));
    br_draw_text(r, "PAUSED", ((float)s->engine->config.width - sz.x) / 2, 80, 2, BR_YELLOW);
}
/* transparent: game still draws underneath; update_below=false freezes it */
static const br_scene_desc k_pause_scene = {
    "pause", 0, true, false, NULL, NULL, NULL, NULL, NULL, pause_update, pause_render, NULL
};

static void title_update(br_scene *s, float dt) {
    BR_UNUSED(dt);
    if (br_action_pressed(&s->engine->input, G.act_start)) {
        br_play(&s->engine->mixer, &G.start_sfx, NULL);
        br_scene_switch(&s->engine->scenes, &k_game_scene);
    }
}
static void title_render(br_scene *s) {
    br_renderer *r = &s->engine->renderer;
    float t = (float)s->time;
    br_camera2d cam = br_camera2d_make(br_v2(0, 0), br_v2(160, 70));
    cam.rotation = sinf(t * 2) * 0.1f;
    cam.zoom = 3.0f + sinf(t * 3) * 0.2f;
    br_begin_camera(r, &cam);
    br_draw_text(r, "BRONTO", -24, -4, 1, BR_ORANGE);
    br_end_camera(r);
    br_draw_text(r, "press ENTER", 116, 130, 1, BR_WHITE);
}
static const br_scene_desc k_title_scene = {
    "title", 0, false, false, NULL, NULL, NULL, NULL, NULL, title_update, title_render, NULL
};

/* ---- message subscriber ------------------------------------------------------ */
static bool on_bounce(const br_event *ev, void *user) {
    br_engine *e = (br_engine *)user;
    const bounce_msg *m = BR_EVENT_DATA(ev, bounce_msg);
    br_play_params p = br_play_defaults();
    G.bounces++;
    p.volume = br_clampf(m->speed / 400.0f, 0.05f, 0.6f);
    p.pitch = 0.8f + br_clampf(m->speed / 500.0f, 0, 0.8f);
    br_play(&e->mixer, &G.bounce_sfx, &p);
    return true;
}

static bool on_init(br_engine *e) {
    make_ball_image(&G.ball);
    br_sound_generate(&G.bounce_sfx, BR_WAVE_TRIANGLE, 330, 0.08f, 0.6f, 44100);
    br_sound_generate(&G.start_sfx, BR_WAVE_SQUARE, 660, 0.15f, 0.3f, 44100);
    G.ev_bounced = br_events_register_type(&e->events, "ball_bounced");
    br_events_subscribe(&e->events, G.ev_bounced, on_bounce, e, 0);

    G.act_start = br_input_action(&e->input, "start");
    G.act_pause = br_input_action(&e->input, "pause");
    G.act_spawn = br_input_action(&e->input, "spawn");
    br_input_bind(&e->input, G.act_start, BR_BIND_KEY, BR_KEY_ENTER, -1);
    br_input_bind(&e->input, G.act_start, BR_BIND_PAD_BUTTON, BR_PAD_START, -1);
    br_input_bind(&e->input, G.act_pause, BR_BIND_KEY, BR_KEY_P, -1);
    br_input_bind(&e->input, G.act_pause, BR_BIND_KEY, BR_KEY_ESCAPE, -1);
    br_input_bind(&e->input, G.act_spawn, BR_BIND_MOUSE_BUTTON, BR_MOUSE_LEFT, -1);
    br_input_bind(&e->input, G.act_spawn, BR_BIND_PAD_BUTTON, BR_PAD_A, -1);
    return true;
}

static void on_shutdown(br_engine *e) {
    br_mixer_stop_sound(&e->mixer, &G.bounce_sfx);
    br_mixer_stop_sound(&e->mixer, &G.start_sfx);
    br_sound_destroy(&G.bounce_sfx);
    br_sound_destroy(&G.start_sfx);
    br_image_destroy(&G.ball);
}

/* ---- headless script: drive the game and capture screenshots ---------------- */
static void key_tap(br_key k) {
    br_event d = br_event_key(BR_EVENT_KEY_DOWN, k, 0, false);
    br_event u = br_event_key(BR_EVENT_KEY_UP, k, 0, false);
    br_headless_inject(&d);
    br_headless_inject(&u);
}

static void after_frame(br_engine *e) {
    if (br_key_pressed(&e->input, BR_KEY_F11)) br_window_toggle_fullscreen(e);
}

int main(int argc, char **argv) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config cfg = br_engine_default_config();
    int frame;
    cfg.title = "Bronto - bounce";
    cfg.initial_scene = &k_title_scene;
    cfg.on_init = on_init;
    cfg.on_shutdown = on_shutdown;
    if (argc > 1 && strcmp(argv[1], "--headless") == 0) cfg.platform = br_platform_headless();
    if (!br_engine_init(e, &cfg)) return 1;

    if (e->platform == br_platform_headless()) {
        static int16_t audio[44100 * 6 * 2];
        br_sound captured;
        br_headless_capture_audio(audio, 44100 * 6);
        for (frame = 0; frame < 360 && e->running; ++frame) {
            if (frame == 30) key_tap(BR_KEY_ENTER);                 /* title -> game */
            if (frame == 120) {
                /* platforms report window pixels: (480,120) is (160,40) in the 3x-scaled framebuffer */
                br_event mv = br_event_mouse_move(480, 120, 0, 0);
                br_event click = br_event_mouse_button(BR_EVENT_MOUSE_BUTTON_DOWN, BR_MOUSE_LEFT, 480, 120);
                br_headless_inject(&mv);
                br_headless_inject(&click);
            }
            if (frame == 121) {
                br_event up = br_event_mouse_button(BR_EVENT_MOUSE_BUTTON_UP, BR_MOUSE_LEFT, 480, 120);
                br_headless_inject(&up);
            }
            if (frame == 200) key_tap(BR_KEY_P);                    /* pause */
            if (frame == 260) key_tap(BR_KEY_P);                    /* resume */
            br_engine_frame(e);
            if (frame == 20)  br_image_save_bmp(&e->framebuffer, "bounce_title.bmp");
            if (frame == 180) br_image_save_bmp(&e->framebuffer, "bounce_game.bmp");
            if (frame == 230) br_image_save_bmp(&e->framebuffer, "bounce_paused.bmp");
        }
        captured.samples = audio;
        captured.frames = br_headless_captured_frames();
        captured.channels = 2;
        captured.sample_rate = 44100;
        br_sound_save_wav(&captured, "bounce_audio.wav");
        printf("bounce: ran %d frames headless, %d bounces, %u audio frames.\n"
               "Wrote bounce_title.bmp, bounce_game.bmp, bounce_paused.bmp, bounce_audio.wav\n",
               frame, G.bounces, captured.frames);
    } else {
        br_engine_loop(e, after_frame);   /* (on the web the browser runs the frames) */
    }
    br_engine_shutdown(e);
    br_free(e);
    return 0;
}
