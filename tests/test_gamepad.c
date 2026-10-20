/* Tests: gamepad snapshot diffing, engine integration, rumble. */
#include "test.h"
#include <string.h>

static int count_type(br_events *ev, uint32_t type, br_event *last) {
    size_t i;
    int n = 0;
    for (i = 0; i < br_events_pending(ev); ++i) {
        br_event *e = br_events_peek(ev, i);
        if (e->type == type) { n++; if (last) *last = *e; }
    }
    return n;
}

static void test_gamepad_tracker(void) {
    br_events ev;
    br_gamepad_tracker t;
    br_gamepad_raw raw;
    br_event last;
    memset(&t, 0, sizeof(t));
    memset(&raw, 0, sizeof(raw));
    br_events_init(&ev, 64);

    br_gamepads_sync(&t, 1, &raw, &ev);                  /* nothing plugged in */
    CHECK_EQ_INT(br_events_pending(&ev), 0);

    raw.connected = true;
    strcpy(raw.name, "Test Pad");
    raw.buttons = 1u << BR_PAD_A;
    raw.axes[BR_PAD_AXIS_LEFT_X] = 0.5f;
    br_gamepads_sync(&t, 1, &raw, &ev);
    CHECK_EQ_INT(count_type(&ev, BR_EVENT_GAMEPAD_CONNECTED, &last), 1);
    CHECK(last.u.gamepad.pad == 1 && strcmp(last.u.gamepad.name, "Test Pad") == 0);
    CHECK_EQ_INT(count_type(&ev, BR_EVENT_GAMEPAD_BUTTON_DOWN, &last), 1);
    CHECK(last.u.gamepad.button == BR_PAD_A);
    CHECK_EQ_INT(count_type(&ev, BR_EVENT_GAMEPAD_AXIS, &last), 1);
    CHECK_NEAR(last.u.gamepad.value, 0.5, 0);
    br_events_clear(&ev);

    br_gamepads_sync(&t, 1, &raw, &ev);                  /* unchanged: silent */
    CHECK_EQ_INT(br_events_pending(&ev), 0);
    raw.axes[BR_PAD_AXIS_LEFT_X] = 0.5005f;               /* sensor noise: ignored */
    br_gamepads_sync(&t, 1, &raw, &ev);
    CHECK_EQ_INT(br_events_pending(&ev), 0);

    raw.buttons = (1u << BR_PAD_B) | (1u << BR_PAD_DPAD_UP);
    raw.axes[BR_PAD_AXIS_LEFT_X] = 0.0f;                  /* back to rest: always reported */
    raw.axes[BR_PAD_AXIS_RIGHT_TRIGGER] = 7.0f;           /* out of range: clamped */
    raw.axes[BR_PAD_AXIS_LEFT_TRIGGER] = -1.0f;           /* triggers never go negative */
    br_gamepads_sync(&t, 1, &raw, &ev);
    CHECK_EQ_INT(count_type(&ev, BR_EVENT_GAMEPAD_BUTTON_UP, NULL), 1);
    CHECK_EQ_INT(count_type(&ev, BR_EVENT_GAMEPAD_BUTTON_DOWN, NULL), 2);
    CHECK_EQ_INT(count_type(&ev, BR_EVENT_GAMEPAD_AXIS, NULL), 2);
    CHECK_NEAR(t.prev[1].axes[BR_PAD_AXIS_RIGHT_TRIGGER], 1.0, 0);
    CHECK_NEAR(t.prev[1].axes[BR_PAD_AXIS_LEFT_TRIGGER], 0.0, 0);
    br_events_clear(&ev);

    /* unplugged while holding: everything is released, then disconnect */
    raw.connected = false;
    br_gamepads_sync(&t, 1, &raw, &ev);
    CHECK_EQ_INT(count_type(&ev, BR_EVENT_GAMEPAD_BUTTON_UP, NULL), 2);
    CHECK_EQ_INT(count_type(&ev, BR_EVENT_GAMEPAD_AXIS, &last), 1);
    CHECK(last.u.gamepad.axis == BR_PAD_AXIS_RIGHT_TRIGGER && last.u.gamepad.value == 0.0f);
    CHECK(br_events_peek(&ev, br_events_pending(&ev) - 1)->type == BR_EVENT_GAMEPAD_DISCONNECTED);
    br_events_clear(&ev);
    br_gamepads_sync(&t, 1, &raw, &ev);
    CHECK_EQ_INT(br_events_pending(&ev), 0);
    br_gamepads_sync(&t, 9, &raw, &ev);                  /* bad index: ignored */
    br_events_destroy(&ev);
}

static void test_gamepad_engine_and_rumble(void) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config c = br_engine_default_config();
    br_gamepad_raw raw;
    br_action jump, left;
    float lo, hi;
    int i;
    c.platform = br_platform_headless();
    c.quit_when_no_scenes = false;
    CHECK(br_engine_init(e, &c));
    jump = br_input_action(&e->input, "jump");
    left = br_input_action(&e->input, "left");
    br_input_bind(&e->input, jump, BR_BIND_PAD_BUTTON, BR_PAD_A, -1);
    br_input_bind(&e->input, left, BR_BIND_PAD_AXIS_NEG, BR_PAD_AXIS_LEFT_X, -1);

    CHECK(!br_gamepad_rumble(e, 2, 1, 1, 1));            /* nothing connected */

    memset(&raw, 0, sizeof(raw));
    raw.connected = true;
    strcpy(raw.name, "Virtual Controller");
    raw.buttons = 1u << BR_PAD_A;
    raw.axes[BR_PAD_AXIS_LEFT_X] = -0.9f;
    br_headless_set_gamepad(2, &raw);
    br_engine_frame(e);
    CHECK(br_pad_connected(&e->input, 2));
    CHECK(strcmp(br_pad_name(&e->input, 2), "Virtual Controller") == 0);
    CHECK(br_pad_pressed(&e->input, 2, BR_PAD_A));
    CHECK(br_action_pressed(&e->input, jump) && br_action_down(&e->input, left));
    CHECK_NEAR(br_pad_axis(&e->input, 2, BR_PAD_AXIS_LEFT_X), -0.875, 1e-5);

    /* rumble for 0.1 s, then the engine switches it off */
    CHECK(br_gamepad_rumble(e, 2, 0.25f, 2.0f, 0.1f));
    br_headless_get_rumble(2, &lo, &hi);
    CHECK_NEAR(lo, 0.25, 0); CHECK_NEAR(hi, 1.0, 0);     /* clamped */
    for (i = 0; i < 4; ++i) br_engine_frame(e);
    br_headless_get_rumble(2, &lo, &hi);
    CHECK_NEAR(lo, 0.25, 0);                              /* still within 0.1 s */
    for (i = 0; i < 4; ++i) br_engine_frame(e);
    br_headless_get_rumble(2, &lo, &hi);
    CHECK(lo == 0 && hi == 0);

    /* unplug: actions release, pad reports disconnected */
    raw.connected = false;
    br_headless_set_gamepad(2, &raw);
    br_engine_frame(e);
    CHECK(!br_pad_connected(&e->input, 2) && br_pad_name(&e->input, 2)[0] == 0);
    CHECK(br_action_released(&e->input, jump) && !br_action_down(&e->input, left));
    br_engine_shutdown(e);
    br_free(e);
}

void suite_gamepad(void) {
    RUN(test_gamepad_tracker);
    RUN(test_gamepad_engine_and_rumble);
}
