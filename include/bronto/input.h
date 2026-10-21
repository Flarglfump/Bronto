/*
 * Bronto Engine - input system.
 *
 * br_input is a pure state machine fed by events (br_input_handle_event),
 * so it knows nothing about the OS: platform backends translate native
 * input into br_events and the engine forwards them here. That also makes
 * input trivially scriptable for tests, replays and networking.
 *
 * Edge queries (pressed/released) are frame based and are reliable even
 * when a key goes down and up within the same frame.
 */
#ifndef BRONTO_INPUT_H
#define BRONTO_INPUT_H

#include "types.h"

/* Key codes: printable keys use their (uppercase) ASCII value. */
typedef enum br_key {
    BR_KEY_UNKNOWN = 0,
    BR_KEY_SPACE = 32,
    BR_KEY_APOSTROPHE = 39,
    BR_KEY_COMMA = 44, BR_KEY_MINUS = 45, BR_KEY_PERIOD = 46, BR_KEY_SLASH = 47,
    BR_KEY_0 = 48, BR_KEY_1, BR_KEY_2, BR_KEY_3, BR_KEY_4,
    BR_KEY_5, BR_KEY_6, BR_KEY_7, BR_KEY_8, BR_KEY_9,
    BR_KEY_SEMICOLON = 59, BR_KEY_EQUAL = 61,
    BR_KEY_A = 65, BR_KEY_B, BR_KEY_C, BR_KEY_D, BR_KEY_E, BR_KEY_F, BR_KEY_G,
    BR_KEY_H, BR_KEY_I, BR_KEY_J, BR_KEY_K, BR_KEY_L, BR_KEY_M, BR_KEY_N,
    BR_KEY_O, BR_KEY_P, BR_KEY_Q, BR_KEY_R, BR_KEY_S, BR_KEY_T, BR_KEY_U,
    BR_KEY_V, BR_KEY_W, BR_KEY_X, BR_KEY_Y, BR_KEY_Z,
    BR_KEY_LEFT_BRACKET = 91, BR_KEY_BACKSLASH = 92, BR_KEY_RIGHT_BRACKET = 93,
    BR_KEY_GRAVE = 96,

    BR_KEY_ESCAPE = 256, BR_KEY_ENTER, BR_KEY_TAB, BR_KEY_BACKSPACE,
    BR_KEY_INSERT, BR_KEY_DELETE, BR_KEY_RIGHT, BR_KEY_LEFT, BR_KEY_DOWN, BR_KEY_UP,
    BR_KEY_PAGE_UP, BR_KEY_PAGE_DOWN, BR_KEY_HOME, BR_KEY_END,
    BR_KEY_CAPS_LOCK = 280, BR_KEY_SCROLL_LOCK, BR_KEY_NUM_LOCK, BR_KEY_PRINT_SCREEN, BR_KEY_PAUSE,
    BR_KEY_F1 = 290, BR_KEY_F2, BR_KEY_F3, BR_KEY_F4, BR_KEY_F5, BR_KEY_F6,
    BR_KEY_F7, BR_KEY_F8, BR_KEY_F9, BR_KEY_F10, BR_KEY_F11, BR_KEY_F12,
    BR_KEY_KP_0 = 320, BR_KEY_KP_1, BR_KEY_KP_2, BR_KEY_KP_3, BR_KEY_KP_4,
    BR_KEY_KP_5, BR_KEY_KP_6, BR_KEY_KP_7, BR_KEY_KP_8, BR_KEY_KP_9,
    BR_KEY_KP_DECIMAL, BR_KEY_KP_DIVIDE, BR_KEY_KP_MULTIPLY, BR_KEY_KP_SUBTRACT,
    BR_KEY_KP_ADD, BR_KEY_KP_ENTER, BR_KEY_KP_EQUAL,
    BR_KEY_LEFT_SHIFT = 340, BR_KEY_LEFT_CONTROL, BR_KEY_LEFT_ALT, BR_KEY_LEFT_SUPER,
    BR_KEY_RIGHT_SHIFT, BR_KEY_RIGHT_CONTROL, BR_KEY_RIGHT_ALT, BR_KEY_RIGHT_SUPER,
    BR_KEY_MENU,
    BR_KEY_COUNT = 512
} br_key;

typedef enum br_key_mod {
    BR_MOD_SHIFT = 1 << 0,
    BR_MOD_CONTROL = 1 << 1,
    BR_MOD_ALT = 1 << 2,
    BR_MOD_SUPER = 1 << 3,
    BR_MOD_CAPS_LOCK = 1 << 4
} br_key_mod;

typedef enum br_mouse_button {
    BR_MOUSE_LEFT = 0, BR_MOUSE_RIGHT, BR_MOUSE_MIDDLE, BR_MOUSE_X1, BR_MOUSE_X2,
    BR_MOUSE_BUTTON_COUNT
} br_mouse_button;

/* ---- Gamepads -----------------------------------------------------------
 * Every controller is presented with the same Xbox-style layout, whatever
 * the hardware (on a PlayStation pad, A = cross, B = circle, X = square,
 * Y = triangle). Sticks report -1..1 with +X right and +Y DOWN (screen
 * convention); triggers report 0..1. */
#define BR_MAX_GAMEPADS 4
#define BR_GAMEPAD_NAME_MAX 64
typedef enum br_gamepad_button {
    BR_PAD_A = 0, BR_PAD_B, BR_PAD_X, BR_PAD_Y,
    BR_PAD_LEFT_BUMPER, BR_PAD_RIGHT_BUMPER, BR_PAD_BACK, BR_PAD_START, BR_PAD_GUIDE,
    BR_PAD_LEFT_STICK, BR_PAD_RIGHT_STICK,
    BR_PAD_DPAD_UP, BR_PAD_DPAD_RIGHT, BR_PAD_DPAD_DOWN, BR_PAD_DPAD_LEFT,
    BR_PAD_BUTTON_COUNT
} br_gamepad_button;

typedef enum br_gamepad_axis {
    BR_PAD_AXIS_LEFT_X = 0, BR_PAD_AXIS_LEFT_Y, BR_PAD_AXIS_RIGHT_X, BR_PAD_AXIS_RIGHT_Y,
    BR_PAD_AXIS_LEFT_TRIGGER, BR_PAD_AXIS_RIGHT_TRIGGER,
    BR_PAD_AXIS_COUNT
} br_gamepad_axis;

/* ---- Action mapping ---------------------------------------------------- */
#define BR_MAX_ACTIONS           64
#define BR_MAX_ACTION_BINDINGS   6
#define BR_ACTION_NAME_MAX       32
#define BR_TEXT_INPUT_MAX        64

typedef int br_action;  /* -1 = invalid */

typedef enum br_binding_kind {
    BR_BIND_NONE = 0,
    BR_BIND_KEY,
    BR_BIND_MOUSE_BUTTON,
    BR_BIND_PAD_BUTTON,
    BR_BIND_PAD_AXIS_POS,   /* axis > deadzone  */
    BR_BIND_PAD_AXIS_NEG    /* axis < -deadzone */
} br_binding_kind;

typedef struct br_binding {
    br_binding_kind kind;
    int code;     /* key / button / axis */
    int pad;      /* gamepad index, -1 = any */
} br_binding;

typedef struct br_action_def {
    char name[BR_ACTION_NAME_MAX];
    br_binding bindings[BR_MAX_ACTION_BINDINGS];
    int binding_count;
    bool down, prev_down;
} br_action_def;

typedef struct br_gamepad_state {
    bool connected;
    char name[BR_GAMEPAD_NAME_MAX];
    bool buttons[BR_PAD_BUTTON_COUNT];
    bool pressed[BR_PAD_BUTTON_COUNT];
    bool released[BR_PAD_BUTTON_COUNT];
    float axes[BR_PAD_AXIS_COUNT];
    /* The triggers as buttons (0 left, 1 right): down past half way, up again
     * below 0.4; worked out by br_input_end_events. */
    bool triggers[2];
    bool trigger_pressed[2];
    bool trigger_released[2];
} br_gamepad_state;

typedef struct br_input {
    bool keys[BR_KEY_COUNT];
    bool keys_pressed[BR_KEY_COUNT];
    bool keys_released[BR_KEY_COUNT];
    bool keys_repeated[BR_KEY_COUNT];
    uint32_t mods;

    float mouse_x, mouse_y;
    float mouse_dx, mouse_dy;
    float wheel_x, wheel_y;
    bool mouse[BR_MOUSE_BUTTON_COUNT];
    bool mouse_pressed[BR_MOUSE_BUTTON_COUNT];
    bool mouse_released[BR_MOUSE_BUTTON_COUNT];

    br_gamepad_state pads[BR_MAX_GAMEPADS];
    float deadzone;

    uint32_t text[BR_TEXT_INPUT_MAX]; /* UTF-32 codepoints typed this frame */
    int text_count;

    br_action_def actions[BR_MAX_ACTIONS];
    int action_count;
} br_input;

struct br_event;

void br_input_init(br_input *in);
/* Call once per frame BEFORE feeding the frame's events. Clears edges. */
void br_input_begin_frame(br_input *in);
/* Feed one event. Non-input events are ignored. */
void br_input_handle_event(br_input *in, const struct br_event *e);
/* Call after all events for the frame were fed; resolves actions. */
void br_input_end_events(br_input *in);
/* Release everything (e.g. on focus loss). */
void br_input_reset(br_input *in);

bool br_key_down(const br_input *in, br_key k);
bool br_key_pressed(const br_input *in, br_key k);
bool br_key_released(const br_input *in, br_key k);
bool br_key_repeated(const br_input *in, br_key k); /* pressed or OS auto-repeat */

bool br_mouse_down(const br_input *in, br_mouse_button b);
bool br_mouse_pressed(const br_input *in, br_mouse_button b);
bool br_mouse_released(const br_input *in, br_mouse_button b);

bool  br_pad_connected(const br_input *in, int pad);
bool  br_pad_down(const br_input *in, int pad, br_gamepad_button b);
bool  br_pad_pressed(const br_input *in, int pad, br_gamepad_button b);
bool  br_pad_released(const br_input *in, int pad, br_gamepad_button b);
float br_pad_axis(const br_input *in, int pad, br_gamepad_axis a); /* deadzone applied */

/* Actions: named, rebindable logical inputs ("jump", "move_left"...). */
br_action br_input_action(br_input *in, const char *name);  /* find or create */
br_action br_input_find_action(const br_input *in, const char *name);
bool br_input_bind(br_input *in, br_action a, br_binding_kind kind, int code, int pad);
void br_input_clear_bindings(br_input *in, br_action a);
bool br_action_down(const br_input *in, br_action a);
bool br_action_pressed(const br_input *in, br_action a);
bool br_action_released(const br_input *in, br_action a);
/* -1..1 from two digital actions (e.g. left/right). */
float br_action_axis(const br_input *in, br_action negative, br_action positive);

const char *br_key_name(br_key k);
const char *br_pad_name(const br_input *in, int pad);   /* "" when not connected */

/* ---- Gamepad snapshots (for platform backends) -------------------------
 * Backends read each controller as a snapshot (buttons bitmask + axes) and
 * hand it to br_gamepads_sync, which diffs against the previous snapshot
 * and posts the matching GAMEPAD_* events. Keeping this in portable code
 * means every platform produces exactly the same event stream. */
typedef struct br_gamepad_raw {
    bool connected;
    uint32_t buttons;                    /* bit (1u << br_gamepad_button) */
    float axes[BR_PAD_AXIS_COUNT];
    char name[BR_GAMEPAD_NAME_MAX];
} br_gamepad_raw;

typedef struct br_gamepad_tracker {
    br_gamepad_raw prev[BR_MAX_GAMEPADS];
} br_gamepad_tracker;

struct br_events;
void br_gamepads_sync(br_gamepad_tracker *t, int pad, const br_gamepad_raw *now, struct br_events *events);

#endif /* BRONTO_INPUT_H */
