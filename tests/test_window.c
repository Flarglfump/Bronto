/* Tests: window management (viewport maths + engine window handling on the
 * headless platform's simulated window). */
#include "test.h"
#include <string.h>

static void test_viewport_modes(void) {
    br_viewport v;
    br_vec2 p;
    int fw, fh;
    /* FIT: 320x180 in 1000x600 -> scale 3.125, height 563, letterboxed */
    v = br_viewport_compute(320, 180, 1000, 600, BR_SCALE_FIT);
    CHECK(v.rect.x == 0 && v.rect.w == 1000 && v.rect.h == 563 && v.rect.y == 18);
    CHECK_NEAR(v.scale_x, 3.125, 1e-6);
    /* pillarbox */
    v = br_viewport_compute(320, 180, 1280, 540, BR_SCALE_FIT);
    CHECK(v.rect.x == 160 && v.rect.y == 0 && v.rect.w == 960 && v.rect.h == 540);
    /* INTEGER: largest whole multiple, centred */
    v = br_viewport_compute(320, 180, 1000, 600, BR_SCALE_INTEGER);
    CHECK(v.rect.x == 20 && v.rect.y == 30 && v.rect.w == 960 && v.rect.h == 540);
    CHECK_NEAR(v.scale_x, 3, 0);
    /* INTEGER with a window smaller than the framebuffer falls back to FIT */
    v = br_viewport_compute(320, 180, 200, 100, BR_SCALE_INTEGER);
    CHECK(v.rect.h == 100 && v.rect.w == 178 && v.rect.x == 11);
    /* STRETCH */
    v = br_viewport_compute(320, 180, 1000, 600, BR_SCALE_STRETCH);
    CHECK(v.rect.x == 0 && v.rect.y == 0 && v.rect.w == 1000 && v.rect.h == 600);
    CHECK_NEAR(v.scale_y, 600.0 / 180.0, 1e-6);
    /* degenerate (minimised windows report 0x0) */
    v = br_viewport_compute(320, 180, 0, 0, BR_SCALE_FIT);
    CHECK(v.rect.w == 0 && v.rect.h == 0);
    /* mapping round trip */
    v = br_viewport_compute(320, 180, 1280, 540, BR_SCALE_FIT);
    p = br_viewport_to_framebuffer(&v, br_v2(160 + 30, 60));
    CHECK_NEAR(p.x, 10, 1e-5); CHECK_NEAR(p.y, 20, 1e-5);
    p = br_viewport_to_window(&v, p);
    CHECK_NEAR(p.x, 190, 1e-4); CHECK_NEAR(p.y, 60, 1e-4);
    br_viewport_resize_dims(1000, 600, 3, &fw, &fh);
    CHECK(fw == 333 && fh == 200);
    br_viewport_resize_dims(1, 1, 3, &fw, &fh);
    CHECK(fw == 1 && fh == 1);
}

/* A scene that counts updates and can veto window close. */
typedef struct win_scene { int updates, closes; bool veto_close; } win_scene;
static win_scene *g_ws;
static bool ws_enter(br_scene *s) { g_ws = (win_scene *)s->data; return true; }
static void ws_update(br_scene *s, float dt) { BR_UNUSED(dt); ((win_scene *)s->data)->updates++; }
static bool ws_event(br_scene *s, const br_event *e) {
    win_scene *d = (win_scene *)s->data;
    if (e->type == BR_EVENT_WINDOW_CLOSE_REQUESTED) { d->closes++; return d->veto_close; }
    return false;
}
static const br_scene_desc k_ws = { "win", sizeof(win_scene), false, false, ws_enter, NULL, NULL, NULL, NULL, ws_update, NULL, ws_event };

static br_engine *start(br_engine_config *c) {
    br_engine *e = BR_NEW(br_engine);
    c->platform = br_platform_headless();
    c->initial_scene = &k_ws;
    CHECK(br_engine_init(e, c));
    return e;
}
static void stop(br_engine *e) { br_engine_shutdown(e); br_free(e); }
static void inject(uint32_t type) { br_event ev = br_event_make(type); br_headless_inject(&ev); }

static void test_engine_window_resize_and_mouse(void) {
    br_engine_config c = br_engine_default_config();
    br_engine *e = start(&c);
    br_recti vp;
    br_event mv;
    CHECK(e->window.width == 960 && e->window.height == 540);
    CHECK(e->window.viewport.rect.w == 960 && e->window.viewport.rect.x == 0);
    br_engine_frame(e);
    vp = br_headless_last_viewport();
    CHECK(vp.w == 960 && vp.h == 540);

    br_headless_resize(1280, 540);              /* user drags the window wider */
    mv = br_event_mouse_move(160 + 30, 60, 3, 6);
    br_headless_inject(&mv);
    br_engine_frame(e);
    vp = br_headless_last_viewport();
    CHECK(vp.x == 160 && vp.y == 0 && vp.w == 960 && vp.h == 540);
    CHECK_NEAR(e->input.mouse_x, 10, 1e-4);    /* window px -> framebuffer px */
    CHECK_NEAR(e->input.mouse_y, 20, 1e-4);

    br_window_set_scale_mode(e, BR_SCALE_INTEGER);
    br_headless_resize(1000, 600);
    br_engine_frame(e);
    vp = br_headless_last_viewport();
    CHECK(vp.x == 20 && vp.y == 30 && vp.w == 960 && vp.h == 540);

    br_window_set_scale_mode(e, BR_SCALE_STRETCH);
    br_engine_frame(e);
    vp = br_headless_last_viewport();
    CHECK(vp.x == 0 && vp.y == 0 && vp.w == 1000 && vp.h == 600);

    /* explicit size request goes through the platform and is clamped */
    br_window_set_size(e, 10, 10);
    br_engine_frame(e);
    CHECK(e->window.width == 320 && e->window.height == 180);   /* min = framebuffer */
    br_window_set_size(e, 640, 360);
    br_engine_frame(e);
    CHECK(e->window.width == 640 && e->window.height == 360);
    stop(e);
}

static void test_resize_mode_follows_window(void) {
    br_engine_config c = br_engine_default_config();
    br_engine *e;
    c.scale_mode = BR_SCALE_RESIZE;
    c.pixel_scale = 2;
    e = start(&c);
    CHECK(e->framebuffer.width == 480 && e->framebuffer.height == 270);   /* 960x540 / 2 */
    br_headless_resize(1280, 720);
    br_engine_frame(e);
    CHECK(e->framebuffer.width == 640 && e->framebuffer.height == 360);
    CHECK(e->renderer.target == &e->framebuffer && e->renderer.clip.w == 640);
    CHECK(br_headless_last_frame()->width == 640);
    CHECK(br_headless_last_viewport().w == 1280);
    stop(e);
}

static void test_close_request_and_veto(void) {
    br_engine_config c = br_engine_default_config();
    br_engine *e = start(&c);
    g_ws->veto_close = true;
    inject(BR_EVENT_WINDOW_CLOSE_REQUESTED);
    CHECK(br_engine_frame(e));                 /* scene consumed it */
    CHECK_EQ_INT(g_ws->closes, 1);
    g_ws->veto_close = false;
    inject(BR_EVENT_WINDOW_CLOSE_REQUESTED);
    CHECK(!br_engine_frame(e));                /* now it quits */
    stop(e);

    c.close_quits = false;                     /* game handles closing itself */
    e = start(&c);
    inject(BR_EVENT_WINDOW_CLOSE_REQUESTED);
    CHECK(br_engine_frame(e));
    stop(e);
}

static void test_fullscreen_toggle(void) {
    br_engine_config c = br_engine_default_config();
    br_engine *e = start(&c);
    br_window_toggle_fullscreen(e);
    br_engine_frame(e);
    CHECK(br_window_is_fullscreen(e));
    CHECK(e->window.width == 1920 && e->window.height == 1080);
    CHECK(br_headless_last_viewport().w == 1920);   /* 320x180 * 6 */
    br_window_set_fullscreen(e, true);              /* no-op when already on */
    br_window_toggle_fullscreen(e);
    br_engine_frame(e);
    CHECK(!br_window_is_fullscreen(e));
    CHECK(e->window.width == 960 && e->window.height == 540);
    stop(e);
}

static void test_minimize_focus_and_limiter(void) {
    br_engine_config c = br_engine_default_config();
    br_engine *e = start(&c);
    uint64_t presents;
    int updates;
    double slept0 = br_headless_slept();
    br_engine_frame(e);
    br_engine_frame(e);
    CHECK_NEAR(br_headless_slept() - slept0, 2.0 / 60.0, 1e-9);  /* limiter at 60 fps */

    inject(BR_EVENT_WINDOW_FOCUS_LOST);
    inject(BR_EVENT_MOUSE_ENTER);
    br_engine_frame(e);
    CHECK(!br_window_is_focused(e) && e->window.mouse_inside);

    inject(BR_EVENT_WINDOW_MINIMIZED);
    br_engine_frame(e);                         /* the frame that sees it is skipped */
    presents = br_headless_present_count();
    updates = g_ws->updates;
    slept0 = br_headless_slept();
    br_engine_frame(e);
    br_engine_frame(e);
    CHECK(br_window_is_minimized(e));
    CHECK_EQ_INT(br_headless_present_count(), presents);   /* nothing drawn */
    CHECK_EQ_INT(g_ws->updates, updates);                   /* game paused   */
    CHECK_NEAR(br_headless_slept() - slept0, 2.0 / 20.0, 1e-9);  /* throttled to 20 fps */

    inject(BR_EVENT_WINDOW_RESTORED);
    inject(BR_EVENT_WINDOW_FOCUS_GAINED);
    br_engine_frame(e);
    CHECK(!br_window_is_minimized(e) && br_window_is_focused(e));
    CHECK_EQ_INT(g_ws->updates, updates + 1);
    CHECK_EQ_INT(br_headless_present_count(), presents + 1);
    stop(e);
}

static void test_title_cursor_hidpi(void) {
    br_engine_config c = br_engine_default_config();
    br_engine *e;
    c.title = "Window Test";
    br_headless_set_scale(2.0f);                /* e.g. a Retina display */
    e = start(&c);
    CHECK(strcmp(br_headless_title(), "Window Test") == 0);
    CHECK(e->window.width == 1920 && e->window.height == 1080);   /* 960x540 points */
    CHECK_NEAR(br_window_scale(e), 2, 0);
    br_window_set_title(e, "Renamed");
    CHECK(strcmp(br_headless_title(), "Renamed") == 0);
    br_window_set_cursor_visible(e, false);
    CHECK(!br_headless_cursor_visible() && !e->window.cursor_visible);
    br_headless_set_scale(1.0f);                /* dragged to a normal monitor */
    br_headless_resize(960, 540);
    br_engine_frame(e);
    CHECK_NEAR(br_window_scale(e), 1, 0);
    CHECK(br_window_state_get(e)->viewport.rect.w == 960);
    stop(e);
}

void suite_window(void) {
    RUN(test_viewport_modes);
    RUN(test_engine_window_resize_and_mouse);
    RUN(test_resize_mode_follows_window);
    RUN(test_close_request_and_veto);
    RUN(test_fullscreen_toggle);
    RUN(test_minimize_focus_and_limiter);
    RUN(test_title_cursor_hidpi);
}
