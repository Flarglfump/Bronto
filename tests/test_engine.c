/* Tests: full engine loop on the headless platform. */
#include "test.h"
#include <string.h>

typedef struct Pos { float x, y; } Pos;
typedef struct Vel { float x, y; } Vel;

typedef struct play_data {
    br_component POS, VEL;
    br_entity player;
    br_action left, right, jump;
    br_sound blip;
    int fixed_steps, updates, renders, jumps, quit_seen;
    int fixed_presses;                /* space presses seen by fixed steps */
    bool veto_quit;
} play_data;

static play_data *g_last_play;   /* lets the test inspect scene state */

static void sys_move(br_world *w, float dt, void *u) {
    play_data *d = (play_data *)u;
    br_component q[2];
    br_query it;
    q[0] = d->POS; q[1] = d->VEL;
    it = br_query_make(w, q, 2);
    while (br_query_next(&it)) {
        Pos *p = BR_QUERY_GET(&it, 0, Pos);
        Vel *v = BR_QUERY_GET(&it, 1, Vel);
        p->x += v->x * dt;
        p->y += v->y * dt;
    }
}

static void sys_draw(br_world *w, float dt, void *u) {
    play_data *d = (play_data *)u;
    br_engine *e = BR_WORLD_ENGINE(w);
    br_query it = br_query_make(w, &d->POS, 1);
    BR_UNUSED(dt);
    while (br_query_next(&it)) {
        Pos *p = BR_QUERY_GET(&it, 0, Pos);
        br_fill_rect(&e->renderer, br_rect_make(p->x, p->y, 8, 8), BR_GREEN);
    }
    d->renders++;
}

static bool play_enter(br_scene *s) {
    play_data *d = (play_data *)s->data;
    br_engine *e = s->engine;
    g_last_play = d;
    d->POS = BR_COMPONENT(&s->world, Pos);
    d->VEL = BR_COMPONENT(&s->world, Vel);
    d->player = br_entity_create(&s->world);
    BR_ADD(&s->world, d->player, d->POS, Pos, { 100, 50 });
    BR_ADD(&s->world, d->player, d->VEL, Vel, { 0, 0 });
    d->left = br_input_action(&e->input, "left");
    d->right = br_input_action(&e->input, "right");
    d->jump = br_input_action(&e->input, "jump");
    br_input_bind(&e->input, d->left, BR_BIND_KEY, BR_KEY_A, -1);
    br_input_bind(&e->input, d->right, BR_BIND_KEY, BR_KEY_D, -1);
    br_input_bind(&e->input, d->jump, BR_BIND_KEY, BR_KEY_SPACE, -1);
    br_sound_generate(&d->blip, BR_WAVE_SQUARE, 880, 0.1f, 0.4f, 44100);
    br_system_add(&s->world, "move", BR_PHASE_FIXED_UPDATE, sys_move, d, 0);
    br_system_add(&s->world, "draw", BR_PHASE_RENDER, sys_draw, d, 0);
    return true;
}

static void play_exit(br_scene *s) {
    play_data *d = (play_data *)s->data;
    br_mixer_stop_sound(&s->engine->mixer, &d->blip);
    br_sound_destroy(&d->blip);
}

static void play_fixed(br_scene *s, float dt) {
    play_data *d = (play_data *)s->data;
    Vel *v = BR_GET(&s->world, d->player, d->VEL, Vel);
    BR_UNUSED(dt);
    v->x = 60.0f * br_action_axis(&s->engine->input, d->left, d->right);
    d->fixed_steps++;
    if (br_key_pressed(&s->engine->input, BR_KEY_SPACE)) d->fixed_presses++;
}

static void play_update(br_scene *s, float dt) {
    play_data *d = (play_data *)s->data;
    BR_UNUSED(dt);
    d->updates++;
    if (br_action_pressed(&s->engine->input, d->jump)) {
        d->jumps++;
        br_play(&s->engine->mixer, &d->blip, NULL);
    }
}

static bool play_event(br_scene *s, const br_event *e) {
    play_data *d = (play_data *)s->data;
    if (e->type == BR_EVENT_QUIT) { d->quit_seen++; return d->veto_quit; }
    return false;
}

static const br_scene_desc k_play = {
    "play", sizeof(play_data), false, false,
    play_enter, play_exit, NULL, NULL, play_fixed, play_update, NULL, play_event
};

static br_engine_config headless_config(void) {
    br_engine_config c = br_engine_default_config();
    c.platform = br_platform_headless();
    c.initial_scene = &k_play;
    c.clear_color = br_rgb(10, 20, 30);
    return c;
}

static void test_engine_loop_and_timing(void) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config c = headless_config();
    const br_image *fb;
    c.max_frames = 120;
    br_headless_set_timestep(1.0 / 60.0);
    CHECK(br_engine_init(e, &c));
    while (br_engine_frame(e)) {}
    CHECK_EQ_INT(e->frame, 120);
    CHECK_NEAR(e->time, 2.0, 1e-9);
    CHECK(e->fixed_steps >= 119 && e->fixed_steps <= 120);
    CHECK_EQ_INT(g_last_play->updates, 120);
    CHECK_EQ_INT(g_last_play->renders, 120);
    CHECK_EQ_INT(br_headless_present_count(), 120);
    fb = br_headless_last_frame();
    CHECK(fb == &e->framebuffer && fb->width == 320 && fb->height == 180);
    CHECK(br_color_eq(br_image_get(fb, 0, 0), br_rgb(10, 20, 30)));
    CHECK(br_color_eq(br_image_get(fb, 104, 54), BR_GREEN));   /* player box */
    br_engine_shutdown(e);
    br_free(e);
}

static void test_fixed_step_slow_frames(void) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config c = headless_config();
    c.max_frames = 10;
    br_headless_set_timestep(1.0 / 20.0);   /* 20 fps -> 3 fixed steps per frame */
    br_engine_init(e, &c);
    while (br_engine_frame(e)) {}
    CHECK(e->fixed_steps >= 29 && e->fixed_steps <= 30);
    CHECK_EQ_INT(g_last_play->updates, 10);
    br_engine_shutdown(e);
    /* A huge hitch is clamped and the backlog dropped (no spiral of death). */
    c.max_frames = 3;
    br_headless_set_timestep(5.0);
    br_engine_init(e, &c);
    while (br_engine_frame(e)) {}
    CHECK(e->fixed_steps <= 3 * 5);
    CHECK_NEAR(e->dt, 0.25, 1e-9);
    br_engine_shutdown(e);
    br_headless_set_timestep(1.0 / 60.0);
    br_free(e);
}

/* A press is seen by exactly one fixed step: not by all three of a slow
 * frame, and not lost by a fast frame that runs none (it waits for the next
 * step). The variable update sees it in its own frame either way. */
static void test_fixed_steps_see_each_press_once(void) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config c = headless_config();
    br_event down = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_SPACE, 0, false);
    br_event up = br_event_key(BR_EVENT_KEY_UP, BR_KEY_SPACE, 0, false);
    int i, steps_before;
    br_headless_set_timestep(1.0 / 20.0);   /* 3 fixed steps a frame */
    br_engine_init(e, &c);
    br_engine_frame(e);
    br_headless_inject(&down);
    br_engine_frame(e);
    CHECK_EQ_INT(g_last_play->fixed_presses, 1);
    CHECK_EQ_INT(g_last_play->jumps, 1);
    CHECK(br_key_pressed(&e->input, BR_KEY_SPACE));       /* (still this frame's, after the fixed steps) */
    br_headless_inject(&up);
    br_engine_frame(e);
    br_headless_set_timestep(1.0 / 240.0);  /* a fixed step every fourth frame */
    for (i = 0; i < 8 && !(e->accumulator < 1e-9); ++i) br_engine_frame(e);
    steps_before = (int)e->fixed_steps;
    br_headless_inject(&down);
    br_engine_frame(e);                     /* (no fixed step in this frame) */
    CHECK_EQ_INT((int)e->fixed_steps, steps_before);
    CHECK_EQ_INT(g_last_play->jumps, 2);
    for (i = 0; i < 4; ++i) br_engine_frame(e);
    CHECK((int)e->fixed_steps == steps_before + 1);
    CHECK_EQ_INT(g_last_play->fixed_presses, 2);           /* seen once, a few frames late */
    CHECK_EQ_INT(g_last_play->jumps, 2);
    br_engine_shutdown(e);
    br_headless_set_timestep(1.0 / 60.0);
    br_free(e);
}

static void test_input_drives_gameplay_and_audio(void) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config c = headless_config();
    static int16_t audio[44100 * 2];
    Pos *p;
    int i;
    double energy = 0;
    br_headless_set_timestep(1.0 / 60.0);
    br_headless_capture_audio(audio, 44100);
    br_engine_init(e, &c);
    br_engine_frame(e);                     /* frame 1: idle */
    br_headless_inject(&(br_event){ BR_EVENT_KEY_DOWN, 0, { { 0 } } });  /* ignored key 0 */
    {
        br_event d = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_D, 0, false);
        br_event j = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_SPACE, 0, false);
        br_event ju = br_event_key(BR_EVENT_KEY_UP, BR_KEY_SPACE, 0, false);
        br_headless_inject(&d);
        br_headless_inject(&j);
        br_headless_inject(&ju);            /* tap within one frame */
    }
    for (i = 0; i < 30; ++i) br_engine_frame(e);
    {
        br_event du = br_event_key(BR_EVENT_KEY_UP, BR_KEY_D, 0, false);
        br_headless_inject(&du);
    }
    for (i = 0; i < 29; ++i) br_engine_frame(e);
    p = BR_GET(&br_scenes_top(&e->scenes)->world, g_last_play->player, g_last_play->POS, Pos);
    /* ~30 fixed steps moving right at 60 px/s => ~30 px */
    CHECK(p->x > 128 && p->x < 132);
    CHECK_EQ_INT(g_last_play->jumps, 1);
    /* Audio was pumped at the device rate: 60 frames ~= 1 s of audio. */
    CHECK(br_headless_captured_frames() >= 44099 && br_headless_captured_frames() <= 44100);
    for (i = 0; i < 4410; ++i) energy += fabs((double)audio[(1 * 735 + i) * 2]);
    CHECK(energy > 1000);                   /* the jump blip is audible */
    br_engine_shutdown(e);
    br_free(e);
}

static void test_quit_and_veto(void) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config c = headless_config();
    br_event q = br_event_make(BR_EVENT_QUIT);
    int frames = 0;
    br_engine_init(e, &c);
    g_last_play->veto_quit = true;
    br_headless_inject(&q);
    br_engine_frame(e);
    CHECK(e->running);                      /* scene consumed the quit */
    CHECK_EQ_INT(g_last_play->quit_seen, 1);
    g_last_play->veto_quit = false;
    br_headless_quit_after(5);
    while (br_engine_frame(e)) frames++;
    CHECK(!e->running);
    CHECK_EQ_INT(e->frame, 5);
    CHECK_EQ_INT(frames, 3);   /* frames 2..4 continued, frame 5 stopped */
    br_engine_shutdown(e);
    br_free(e);
}

static int g_runs_initialised;
static bool on_init_hook(br_engine *e) { BR_UNUSED(e); g_runs_initialised++; return true; }

static void test_engine_run_wrapper_and_determinism(void) {
    br_engine_config c = headless_config();
    br_engine *a = BR_NEW(br_engine), *b = BR_NEW(br_engine);
    br_event k = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_A, 0, false);
    int i;
    c.max_frames = 30;
    c.on_init = on_init_hook;
    CHECK_EQ_INT(br_engine_run(&c), 0);
    CHECK_EQ_INT(g_runs_initialised, 1);
    /* Two identical runs produce bit-identical frames. */
    c.max_frames = 0;
    c.on_init = NULL;
    br_engine_init(a, &c);
    br_headless_inject(&k);
    for (i = 0; i < 40; ++i) br_engine_frame(a);
    br_engine_init(b, &c);
    br_headless_inject(&k);
    for (i = 0; i < 40; ++i) br_engine_frame(b);
    CHECK(memcmp(a->framebuffer.pixels, b->framebuffer.pixels, 320 * 180 * sizeof(br_color)) == 0);
    br_engine_shutdown(a);
    br_engine_shutdown(b);
    br_free(a);
    br_free(b);
}

static bool switch_enter(br_scene *s) { BR_UNUSED(s); return true; }
static void switch_update(br_scene *s, float dt) {
    BR_UNUSED(dt);
    if (s->frames == 2) br_scene_pop(&s->engine->scenes);  /* pops itself */
}
static const br_scene_desc k_short = { "short", 0, false, false, switch_enter, NULL, NULL, NULL, NULL, switch_update, NULL, NULL };

static void test_quit_when_no_scenes(void) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config c = headless_config();
    c.initial_scene = &k_short;
    br_engine_init(e, &c);
    while (br_engine_frame(e)) {}
    CHECK_EQ_INT(e->frame, 3);
    CHECK_EQ_INT(br_scenes_depth(&e->scenes), 0);
    br_engine_shutdown(e);
    br_free(e);
}

void suite_engine(void) {
    RUN(test_engine_loop_and_timing);
    RUN(test_fixed_steps_see_each_press_once);
    RUN(test_fixed_step_slow_frames);
    RUN(test_input_drives_gameplay_and_audio);
    RUN(test_quit_and_veto);
    RUN(test_engine_run_wrapper_and_determinism);
    RUN(test_quit_when_no_scenes);
}
