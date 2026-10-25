/*
 * Bronto Engine - on-screen touch controls.
 *
 * For playing on a phone or tablet: a stick and buttons drawn over the
 * window, worked with the fingers. They are a gamepad as far as the game is
 * concerned (a virtual one, in a gamepad slot of its own), so a game that
 * can be played with a gamepad can be played on a touch screen with no
 * changes. Each control can instead press a key, for games that only read
 * the keyboard.
 *
 *     br_touch_ui_configure(&engine->touch, "stick, a, b, x, y, lb, rb, back, start");
 *     br_touch_ui_configure(&engine->touch, "stick=arrows, a=space:JUMP");
 *
 * One item per control: `stick` (the left stick), or a gamepad button by
 * the names scripts use (a b x y, lb rb, back start, lt rt). After `=`, a
 * key to press instead (`space`, `z`, `enter`; for the stick `arrows` or
 * `wasd`); after `:`, a label of up to 6 characters (the default is the
 * button's own name).
 *
 * Where they go is worked out from the window: the stick at the bottom
 * left, A B X Y as a diamond at the bottom right, the shoulder buttons at
 * the top corners, BACK and START beside them. They sit in the black bars
 * around the game when there is room, and over its edges when there isn't.
 * On a screen taller than wide the game moves to the top, and the controls
 * go below it.
 *
 * They show themselves when the screen is first touched (BR_TOUCH_AUTO),
 * and hide again when a key or a real gamepad is used. A touch that misses
 * every control works as the left mouse button, so menus that take clicks
 * take taps.
 */
#ifndef BRONTO_TOUCH_H
#define BRONTO_TOUCH_H

#include "types.h"
#include "input.h"
#include "platform.h"

#define BR_MAX_TOUCH_CONTROLS 12
#define BR_TOUCH_STICK (-1)                    /* br_touch_control.button for the stick */

typedef enum br_touch_mode {
    BR_TOUCH_OFF = 0,     /* never shown; touches are only the mouse */
    BR_TOUCH_AUTO,        /* shown once the screen is touched, hidden by a key or a gamepad (the default) */
    BR_TOUCH_ALWAYS       /* always shown */
} br_touch_mode;

typedef struct br_touch_control {
    int button;                    /* a br_gamepad_button, BR_PAD_BUTTON_COUNT + 0/1 for lt/rt, or BR_TOUCH_STICK */
    br_key keys[4];                /* pressed instead of the gamepad (stick: up, down, left, right); 0 = the gamepad */
    char label[8];
    /* where it is (window pixels), worked out by br_touch_ui_layout */
    float x, y, w, h;              /* centre; size (circles: w = h = the diameter) */
    bool round;
    /* what it is doing */
    int finger;                    /* the touch holding it, -1 = none */
    float sx, sy;                  /* the stick: -1..1 */
    bool down;
} br_touch_control;

typedef struct br_touch_ui {
    br_touch_mode mode;
    bool shown;                    /* on screen now */
    bool hidden;                   /* hidden by the game for now (br_touch_ui_hide) */
    bool mouse_as_touch;           /* the left mouse button acts as a finger (testing on a desktop) */
    br_touch_control controls[BR_MAX_TOUCH_CONTROLS];
    int count;
    /* the fingers down, and what each holds: a control's index, -1 = nothing
     * (it works the mouse), -2 = nothing and not the mouse either */
    struct { int id; int control; } fingers[BR_MAX_TOUCHES];
    int finger_count;
    bool mouse_down;               /* (mouse_as_touch) */
    /* the virtual gamepad */
    int pad;                       /* its slot, -1 until a control needs it */
    br_gamepad_tracker tracker;
    uint8_t keys_held[BR_KEY_COUNT];
    /* the layout's inputs, and what to draw */
    int win_w, win_h;
    float scale;
    br_recti viewport;
    br_overlay_item items[BR_MAX_TOUCH_CONTROLS * 3];
    int item_count;
} br_touch_ui;

void br_touch_ui_init(br_touch_ui *t);

/* Sets the controls from a list (see above). An empty list removes them.
 * Returns false (keeping the old ones) if an item isn't understood; `error`
 * (may be NULL) then says which. */
bool br_touch_ui_configure(br_touch_ui *t, const char *spec, char *error, size_t error_size);
void br_touch_ui_set_mode(br_touch_ui *t, br_touch_mode mode);
/* Hide the controls for now (a cutscene, a menu taken with taps) or show them again. */
void br_touch_ui_hide(br_touch_ui *t, bool hidden);
/* Are they on screen? */
bool br_touch_ui_visible(const br_touch_ui *t);

/* ---- for the engine (engine.c calls these each frame) ---- */

/* Where the game goes: on a tall screen with the controls showing, at the top. */
void br_touch_ui_place_viewport(const br_touch_ui *t, int win_w, int win_h, float scale, br_viewport *vp);
/* Places the controls for this window size and viewport. */
void br_touch_ui_layout(br_touch_ui *t, int win_w, int win_h, float scale, br_recti viewport);
/* Goes through the frame's events before they are dispatched: touches on
 * the controls work them (and are marked claimed), other touches become
 * the mouse, and the virtual gamepad's (or the keys') events are added.
 * Positions are still window pixels. `in` is read for which gamepad slots
 * are free, and told which one is the controls'. */
void br_touch_ui_process(br_touch_ui *t, struct br_events *events, br_input *in);
/* What to draw over the window this frame (count 0 when hidden). */
int br_touch_ui_overlay(const br_touch_ui *t, const br_overlay_item **items);

struct br_renderer;
/* For platforms that can't draw over the window: draws the controls into
 * the framebuffer (the parts over the game). */
void br_touch_ui_draw(const br_touch_ui *t, struct br_renderer *r, const br_viewport *vp);

#endif /* BRONTO_TOUCH_H */
