/* Tests: on-screen touch controls (touch.h), through the engine on the headless platform. */
#include "test.h"
#include <string.h>

static void idle_render(br_scene *s) { BR_UNUSED(s); }
static const br_scene_desc k_idle = { "idle", 0, false, false, NULL, NULL, NULL, NULL, NULL, NULL, idle_render, NULL };

/* An engine in a phone-sized window: 844 x 390 points (landscape), or turned. */
static br_engine *phone(const char *controls, int w, int h, float scale) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config c = br_engine_default_config();
    c.platform = br_platform_headless();
    c.initial_scene = &k_idle;
    c.width = 256;
    c.height = 240;
    c.audio_sample_rate = 0;
    c.touch_controls = controls;
    br_headless_set_timestep(1.0 / 60.0);
    CHECK(br_engine_init(e, &c));
    br_headless_set_scale(scale);
    br_headless_resize((int)((float)w * scale), (int)((float)h * scale));
    br_engine_frame(e);
    return e;
}

static void done(br_engine *e) {
    br_engine_shutdown(e);
    br_free(e);
    br_headless_set_scale(1);
}

static void touch(int type, int id, float x, float y) {
    br_event ev = br_event_touch((uint32_t)type, id, x, y);
    br_headless_inject(&ev);
}

static const br_touch_control *control(const br_engine *e, int button) {
    int i;
    for (i = 0; i < e->touch.count; ++i) if (e->touch.controls[i].button == button) return &e->touch.controls[i];
    return NULL;
}

static void test_touch_list(void) {
    br_touch_ui t;
    char err[200];
    br_touch_ui_init(&t);
    CHECK(br_touch_ui_configure(&t, "stick, a, b, x, y, lb, rb, back, start", err, sizeof(err)));
    CHECK_EQ_INT(t.count, 9);
    CHECK(t.controls[0].button == BR_TOUCH_STICK && t.controls[1].button == BR_PAD_A && strcmp(t.controls[8].label, "START") == 0);
    CHECK(br_touch_ui_configure(&t, " stick = arrows , a=space:JUMP, b = z ,", err, sizeof(err)));
    CHECK_EQ_INT(t.count, 3);
    CHECK(t.controls[0].keys[0] == BR_KEY_UP && t.controls[0].keys[3] == BR_KEY_RIGHT);
    CHECK(t.controls[1].keys[0] == BR_KEY_SPACE && strcmp(t.controls[1].label, "JUMP") == 0);
    CHECK(t.controls[2].keys[0] == BR_KEY_Z && strcmp(t.controls[2].label, "B") == 0);
    CHECK(!br_touch_ui_configure(&t, "stick, jump", err, sizeof(err)) && strstr(err, "unknown touch control 'jump'"));
    CHECK(!br_touch_ui_configure(&t, "a, a", err, sizeof(err)) && strstr(err, "listed twice"));
    CHECK(!br_touch_ui_configure(&t, "a=nokey", err, sizeof(err)) && strstr(err, "unknown key 'nokey'"));
    CHECK(!br_touch_ui_configure(&t, "stick=ijkl", err, sizeof(err)) && strstr(err, "'arrows' or 'wasd'"));
    CHECK_EQ_INT(t.count, 3);                                    /* (a bad list keeps the old one) */
    CHECK(br_touch_ui_configure(&t, "", err, sizeof(err)) && t.count == 0);
    CHECK(!br_touch_ui_visible(&t));
}

/* Landscape: the game in the middle, the controls in the bars beside it; the first touch shows them. */
static void test_touch_landscape_and_gamepad(void) {
    br_engine *e = phone("stick, a, b, x, y, lb, rb, back, start", 844, 390, 2);
    br_recti vp;
    const br_touch_control *stick, *a, *b, *start;
    const br_overlay_item *items;
    CHECK(!br_touch_ui_visible(&e->touch));
    CHECK_EQ_INT(br_touch_ui_overlay(&e->touch, &items), 0);
    touch(BR_EVENT_TOUCH_DOWN, 7, 844, 390);                     /* the first touch: shows them, and is the mouse */
    br_engine_frame(e);
    CHECK(br_touch_ui_visible(&e->touch));
    CHECK(br_mouse_pressed(&e->input, BR_MOUSE_LEFT));
    CHECK_EQ_INT(e->input.last_device, BR_DEVICE_TOUCH);
    CHECK_EQ_INT(e->input.touch_count, 1);
    CHECK(fabsf(e->input.touches[0].x - 128) < 1 && fabsf(e->input.touches[0].y - 120) < 1);   /* (in game pixels) */
    touch(BR_EVENT_TOUCH_UP, 7, 844, 390);
    br_engine_frame(e);
    CHECK(br_mouse_released(&e->input, BR_MOUSE_LEFT));
    vp = e->window.viewport.rect;
    CHECK(vp.y == 0 && vp.h == 780);                             /* (not moved: the screen is wide) */
    stick = control(e, BR_TOUCH_STICK);
    a = control(e, BR_PAD_A);
    b = control(e, BR_PAD_B);
    start = control(e, BR_PAD_START);
    CHECK(stick && a && b && start);
    CHECK(stick->x + stick->w / 2 <= (float)vp.x + 1);            /* in the bars, clear of the game */
    CHECK(a->x - a->w / 2 >= (float)(vp.x + vp.w) - 1 && b->x > a->x && b->y < a->y);
    CHECK(start->x > (float)(vp.x + vp.w) && start->y < a->y);
    CHECK(br_touch_ui_overlay(&e->touch, &items) >= 9 * 2);

    touch(BR_EVENT_TOUCH_DOWN, 1, a->x + 3, a->y - 2);           /* A: the virtual gamepad's A */
    br_engine_frame(e);
    CHECK(br_pad_pressed(&e->input, e->touch.pad, BR_PAD_A) && br_pad_down(&e->input, e->touch.pad, BR_PAD_A));
    CHECK(br_pad_connected(&e->input, e->touch.pad) && strcmp(br_pad_name(&e->input, e->touch.pad), "Touch controls") == 0);
    CHECK_EQ_INT(e->touch.pad, 3);                               /* (the last free slot) */
    CHECK(!br_mouse_pressed(&e->input, BR_MOUSE_LEFT));          /* (a control's touch isn't the mouse) */
    CHECK(e->input.touches[0].claimed);
    CHECK_EQ_INT(e->input.last_device, BR_DEVICE_TOUCH);
    touch(BR_EVENT_TOUCH_DOWN, 2, stick->x + stick->w * 0.4f, stick->y);   /* another finger on the stick: right */
    br_engine_frame(e);
    CHECK(br_pad_axis(&e->input, e->touch.pad, BR_PAD_AXIS_LEFT_X) > 0.9f);
    CHECK(fabsf(br_pad_axis(&e->input, e->touch.pad, BR_PAD_AXIS_LEFT_Y)) < 0.01f);
    CHECK(br_pad_down(&e->input, e->touch.pad, BR_PAD_A));        /* (still held) */
    touch(BR_EVENT_TOUCH_MOVE, 2, stick->x, stick->y - stick->w * 0.2f);   /* half way up */
    br_engine_frame(e);
    CHECK(fabsf(br_pad_axis(&e->input, e->touch.pad, BR_PAD_AXIS_LEFT_X)) < 0.01f);
    CHECK(br_pad_axis(&e->input, e->touch.pad, BR_PAD_AXIS_LEFT_Y) < -0.3f);   /* (0.5, less the dead zone) */
    touch(BR_EVENT_TOUCH_MOVE, 1, b->x, b->y);                   /* sliding from A onto B */
    br_engine_frame(e);
    CHECK(br_pad_released(&e->input, e->touch.pad, BR_PAD_A) && br_pad_pressed(&e->input, e->touch.pad, BR_PAD_B));
    touch(BR_EVENT_TOUCH_UP, 1, b->x, b->y);
    touch(BR_EVENT_TOUCH_UP, 2, stick->x, stick->y);
    br_engine_frame(e);
    CHECK(br_pad_released(&e->input, e->touch.pad, BR_PAD_B));
    CHECK(br_pad_axis(&e->input, e->touch.pad, BR_PAD_AXIS_LEFT_Y) == 0);
    CHECK_EQ_INT(e->input.touch_count, 0);

    br_touch_ui_hide(&e->touch, true);                           /* the game hides them: touches are the mouse */
    br_engine_frame(e);
    CHECK(!br_touch_ui_visible(&e->touch) && br_touch_ui_overlay(&e->touch, &items) == 0);
    touch(BR_EVENT_TOUCH_DOWN, 3, a->x, a->y);
    br_engine_frame(e);
    CHECK(br_mouse_pressed(&e->input, BR_MOUSE_LEFT) && !br_pad_down(&e->input, e->touch.pad, BR_PAD_A));
    touch(BR_EVENT_TOUCH_UP, 3, a->x, a->y);
    br_touch_ui_hide(&e->touch, false);
    br_engine_frame(e);
    CHECK(br_touch_ui_visible(&e->touch));
    {                                                            /* a key: back to the keyboard, so they go */
        br_event k = br_event_make(BR_EVENT_KEY_DOWN);
        k.u.key.key = BR_KEY_X;
        br_headless_inject(&k);
        br_engine_frame(e);
        CHECK(!br_touch_ui_visible(&e->touch));
        CHECK_EQ_INT(e->input.last_device, BR_DEVICE_KEYBOARD);
    }
    done(e);
}

/* Portrait: the game moves to the top, the controls go under it. */
static void test_touch_portrait(void) {
    br_engine *e = phone("stick, a, b, lb, rb, back, start", 390, 844, 3);
    br_recti before = e->window.viewport.rect, vp;
    const br_touch_control *stick, *a, *lb, *back;
    CHECK(before.y > 700);                                        /* (centred while they're hidden) */
    touch(BR_EVENT_TOUCH_DOWN, 1, 100, 100);
    br_engine_frame(e);
    vp = e->window.viewport.rect;
    CHECK(vp.y == 36 && vp.w == 1170);                           /* at the top (12 points down), as wide as the screen */
    stick = control(e, BR_TOUCH_STICK);
    a = control(e, BR_PAD_A);
    lb = control(e, BR_PAD_LEFT_BUMPER);
    back = control(e, BR_PAD_BACK);
    CHECK(lb->y - lb->h / 2 >= (float)(vp.y + vp.h));             /* everything below the game */
    CHECK(back->y - back->h / 2 >= (float)(vp.y + vp.h) && back->x < 1170 / 2);
    CHECK(stick->y > lb->y && a->y > lb->y && stick->x < a->x);
    CHECK(stick->y + stick->w / 2 <= 844 * 3);
    done(e);
}

/* Controls that press keys, for games that read the keyboard. */
static void test_touch_keys(void) {
    br_engine *e = phone("stick=arrows, a=space:JUMP", 844, 390, 1);
    const br_touch_control *stick, *a;
    touch(BR_EVENT_TOUCH_DOWN, 1, 10, 10);
    touch(BR_EVENT_TOUCH_UP, 1, 10, 10);
    br_engine_frame(e);
    stick = control(e, BR_TOUCH_STICK);
    a = control(e, BR_PAD_A);
    touch(BR_EVENT_TOUCH_DOWN, 2, a->x, a->y);
    touch(BR_EVENT_TOUCH_DOWN, 3, stick->x - stick->w * 0.4f, stick->y + stick->w * 0.4f);   /* down and left */
    br_engine_frame(e);
    CHECK(br_key_pressed(&e->input, BR_KEY_SPACE) && br_key_down(&e->input, BR_KEY_LEFT) && br_key_down(&e->input, BR_KEY_DOWN));
    CHECK(!br_key_down(&e->input, BR_KEY_UP) && !br_key_down(&e->input, BR_KEY_RIGHT));
    CHECK(e->touch.pad < 0 && !br_pad_connected(&e->input, 3));  /* (no gamepad needed) */
    CHECK(br_touch_ui_visible(&e->touch));                       /* (its own keys don't hide it) */
    touch(BR_EVENT_TOUCH_UP, 2, a->x, a->y);
    touch(BR_EVENT_TOUCH_MOVE, 3, stick->x + stick->w * 0.4f, stick->y);
    br_engine_frame(e);
    CHECK(br_key_released(&e->input, BR_KEY_SPACE) && br_key_released(&e->input, BR_KEY_LEFT) && br_key_released(&e->input, BR_KEY_DOWN));
    CHECK(br_key_pressed(&e->input, BR_KEY_RIGHT));
    touch(BR_EVENT_TOUCH_UP, 3, 0, 0);
    br_engine_frame(e);
    CHECK(!br_key_down(&e->input, BR_KEY_RIGHT));
    done(e);
}

/* No overlay on this platform: they are drawn into the framebuffer, over the game's edges. The mouse can be a finger. */
static void test_touch_drawn_and_mouse(void) {
    br_engine *e = phone("stick, a", 256, 240, 1);               /* (no bars at all: the game shrinks, and the stick still overlaps it) */
    const br_touch_control *a, *stick;
    br_vec2 p;
    br_color hidden, shown;
    e->touch.mouse_as_touch = true;
    stick = control(e, BR_TOUCH_STICK);
    br_touch_ui_set_mode(&e->touch, BR_TOUCH_ALWAYS);
    br_engine_frame(e);
    CHECK(e->window.viewport.rect.w < 256 && e->window.viewport.rect.w >= 150);   /* (made room, down to 60%) */
    p = br_viewport_to_framebuffer(&e->window.viewport, br_v2(stick->x, stick->y));
    CHECK(p.x > 0 && p.x < 256 && p.y > 0 && p.y < 240);
    shown = br_image_get(br_headless_last_frame(), (int)p.x, (int)p.y);
    br_touch_ui_hide(&e->touch, true);
    br_engine_frame(e);
    hidden = br_image_get(br_headless_last_frame(), (int)p.x, (int)p.y);
    CHECK(!br_color_eq(shown, hidden));                          /* the knob, drawn over the game */
    br_touch_ui_hide(&e->touch, false);
    br_engine_frame(e);
    a = control(e, BR_PAD_A);
    {
        br_event down = br_event_mouse_button(BR_EVENT_MOUSE_BUTTON_DOWN, BR_MOUSE_LEFT, a->x, a->y);
        br_headless_inject(&down);
        br_engine_frame(e);
        CHECK(br_pad_pressed(&e->input, e->touch.pad, BR_PAD_A) && !br_mouse_down(&e->input, BR_MOUSE_LEFT));
    }
    {
        br_event up = br_event_mouse_button(BR_EVENT_MOUSE_BUTTON_UP, BR_MOUSE_LEFT, a->x, a->y);
        br_headless_inject(&up);
        br_engine_frame(e);
        CHECK(br_pad_released(&e->input, e->touch.pad, BR_PAD_A));
    }
    done(e);
}

/* A tablet held sideways: the game makes room so the controls don't cover it. */
static void test_touch_tablet(void) {
    br_engine *e = phone("stick, a, b, x, y", 1024, 768, 2);
    br_recti before = e->window.viewport.rect, vp;
    const br_touch_control *stick, *b;
    touch(BR_EVENT_TOUCH_DOWN, 1, 10, 10);
    br_engine_frame(e);
    vp = e->window.viewport.rect;
    CHECK(vp.w < before.w && vp.w > before.w / 2 && vp.x > before.x);
    CHECK(vp.y > 0 && vp.y + vp.h < 768 * 2);                    /* (still centred) */
    stick = control(e, BR_TOUCH_STICK);
    b = control(e, BR_PAD_B);
    CHECK(stick->x + stick->w / 2 <= (float)vp.x + 1 && b->x - b->w / 2 >= (float)(vp.x + vp.w) - 1);
    done(e);
}

void suite_touch(void) {
    RUN(test_touch_list);
    RUN(test_touch_landscape_and_gamepad);
    RUN(test_touch_portrait);
    RUN(test_touch_keys);
    RUN(test_touch_drawn_and_mouse);
    RUN(test_touch_tablet);
}
