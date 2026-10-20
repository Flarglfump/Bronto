/*
 * Bronto Engine - event queue and message bus.
 *
 * One mechanism serves both OS events (keyboard, window...) and gameplay
 * messages ("player_died", "coin_collected"...):
 *
 *   - br_events_post()  appends to a FIFO queue; delivered on dispatch.
 *   - br_events_emit()  delivers synchronously, right now.
 *   - br_events_subscribe() registers a handler for one type (or all).
 *     Handlers run in priority order (higher first); returning true marks
 *     the event consumed and stops propagation.
 *
 * Custom message types are allocated at runtime with
 * br_events_register_type() and can carry up to BR_EVENT_USER_DATA_SIZE
 * bytes of inline payload (copied, so no lifetime issues) plus a pointer.
 *
 * Subscribing / unsubscribing from inside a handler is safe.
 */
#ifndef BRONTO_EVENT_H
#define BRONTO_EVENT_H

#include "types.h"
#include "input.h"

typedef enum br_event_type {
    BR_EVENT_NONE = 0,
    BR_EVENT_QUIT,
    BR_EVENT_WINDOW_RESIZED,
    BR_EVENT_WINDOW_FOCUS_GAINED,
    BR_EVENT_WINDOW_FOCUS_LOST,
    BR_EVENT_KEY_DOWN,
    BR_EVENT_KEY_UP,
    BR_EVENT_TEXT_INPUT,
    BR_EVENT_MOUSE_MOVE,
    BR_EVENT_MOUSE_BUTTON_DOWN,
    BR_EVENT_MOUSE_BUTTON_UP,
    BR_EVENT_MOUSE_WHEEL,
    BR_EVENT_GAMEPAD_CONNECTED,
    BR_EVENT_GAMEPAD_DISCONNECTED,
    BR_EVENT_GAMEPAD_BUTTON_DOWN,
    BR_EVENT_GAMEPAD_BUTTON_UP,
    BR_EVENT_GAMEPAD_AXIS,
    BR_EVENT_SCENE_CHANGED,
    BR_EVENT_WINDOW_CLOSE_REQUESTED, /* close button / Alt+F4: consume to veto */
    BR_EVENT_WINDOW_MINIMIZED,
    BR_EVENT_WINDOW_MAXIMIZED,
    BR_EVENT_WINDOW_RESTORED,
    BR_EVENT_WINDOW_MOVED,
    BR_EVENT_WINDOW_FULLSCREEN,      /* u.window.fullscreen holds the new state */
    BR_EVENT_WINDOW_SCALE_CHANGED,   /* HiDPI factor changed (moved monitors) */
    BR_EVENT_MOUSE_ENTER,
    BR_EVENT_MOUSE_LEAVE,

    BR_EVENT_BUILTIN_COUNT,
    BR_EVENT_USER = 1024         /* first runtime-registered type */
} br_event_type;

#define BR_EVENT_ANY 0xFFFFFFFFu /* subscribe-to-everything wildcard */

#define BR_EVENT_USER_DATA_SIZE 48
#define BR_MAX_USER_EVENT_TYPES 256

typedef struct br_key_event          { br_key key; int scancode; uint32_t mods; bool repeat; } br_key_event;
typedef struct br_text_event         { uint32_t codepoint; } br_text_event;
/* Mouse positions: platforms report window pixels; before dispatch the
 * engine maps x/y (and dx/dy) into framebuffer pixels using the current
 * viewport, keeping the raw window position in wx/wy. */
typedef struct br_mouse_move_event   { float x, y, dx, dy, wx, wy; } br_mouse_move_event;
typedef struct br_mouse_button_event { br_mouse_button button; float x, y; int clicks; float wx, wy; } br_mouse_button_event;
typedef struct br_mouse_wheel_event  { float dx, dy; } br_mouse_wheel_event;
typedef struct br_resize_event       { int width, height; } br_resize_event; /* window pixels */
typedef struct br_window_event       { int x, y, width, height; float scale; bool fullscreen; } br_window_event;
typedef struct br_gamepad_event      { int pad; int button; int axis; float value; const char *name; } br_gamepad_event;
typedef struct br_user_event         { uint8_t data[BR_EVENT_USER_DATA_SIZE]; void *ptr; } br_user_event;

typedef struct br_event {
    uint32_t type;
    double timestamp;
    union {
        br_key_event key;
        br_text_event text;
        br_mouse_move_event mouse_move;
        br_mouse_button_event mouse_button;
        br_mouse_wheel_event wheel;
        br_resize_event resize;
        br_window_event window;
        br_gamepad_event gamepad;
        br_user_event user;
    } u;
} br_event;

typedef bool (*br_event_handler)(const br_event *e, void *user);
typedef uint32_t br_subscription;  /* 0 = invalid */

typedef struct br_event_sub {
    br_subscription id;
    uint32_t type;
    int priority;
    br_event_handler fn;
    void *user;
    bool active;
    bool pending;   /* added during dispatch; activates afterwards */
} br_event_sub;

typedef struct br_events {
    /* FIFO ring buffer, capacity is a power of two and grows on demand. */
    br_event *queue;
    size_t capacity, head, count;

    br_event_sub *subs;
    size_t sub_count, sub_capacity;
    br_subscription next_sub_id;
    int dispatch_depth;
    bool subs_dirty;

    char *type_names[BR_MAX_USER_EVENT_TYPES];
    uint32_t user_type_count;
    double time;    /* stamped onto posted events with timestamp == 0 */
} br_events;

bool br_events_init(br_events *ev, size_t initial_capacity);
void br_events_destroy(br_events *ev);

/* Registers (or finds) a custom message type by name. 0 on failure. */
uint32_t br_events_register_type(br_events *ev, const char *name);
uint32_t br_events_find_type(const br_events *ev, const char *name);
const char *br_events_type_name(const br_events *ev, uint32_t type);

br_subscription br_events_subscribe(br_events *ev, uint32_t type, br_event_handler fn,
                                    void *user, int priority);
void br_events_unsubscribe(br_events *ev, br_subscription id);

bool br_events_post(br_events *ev, const br_event *e);
/* Post a custom message with a payload copied inline (size <= 48). */
bool br_events_post_data(br_events *ev, uint32_t type, const void *data, size_t size);
/* Deliver immediately. Returns true if a handler consumed it. */
bool br_events_emit(br_events *ev, const br_event *e);

/* Deliver queued events (including those posted while dispatching) to
 * subscribers. Returns how many were delivered. */
size_t br_events_dispatch(br_events *ev);

/* Manual pull API (bypasses subscribers). */
bool br_events_poll(br_events *ev, br_event *out);
size_t br_events_pending(const br_events *ev);
/* The index-th queued event (0 = next to be delivered) or NULL; lets the
 * engine fix up events in place before dispatch. */
br_event *br_events_peek(br_events *ev, size_t index);
void br_events_clear(br_events *ev);

/* Helpers to build common events. */
br_event br_event_make(uint32_t type);
br_event br_event_key(uint32_t type, br_key key, uint32_t mods, bool repeat);
br_event br_event_mouse_button(uint32_t type, br_mouse_button b, float x, float y);
br_event br_event_mouse_move(float x, float y, float dx, float dy);

/* Typed payload access: BR_EVENT_DATA(e, my_struct)->field */
#define BR_EVENT_DATA(e, T) ((const T *)(const void *)(e)->u.user.data)

#endif /* BRONTO_EVENT_H */
