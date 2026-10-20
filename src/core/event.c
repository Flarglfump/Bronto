#include "bronto/event.h"
#include "bronto/memory.h"
#include "bronto/log.h"

#include <string.h>

#define BR_DISPATCH_SAFETY_LIMIT 65536u

static const char *k_builtin_names[BR_EVENT_BUILTIN_COUNT] = {
    "none", "quit", "window_resized", "window_focus_gained", "window_focus_lost",
    "key_down", "key_up", "text_input", "mouse_move", "mouse_button_down",
    "mouse_button_up", "mouse_wheel", "gamepad_connected", "gamepad_disconnected",
    "gamepad_button_down", "gamepad_button_up", "gamepad_axis", "scene_changed",
    "window_close_requested", "window_minimized", "window_maximized", "window_restored",
    "window_moved", "window_fullscreen", "window_scale_changed", "mouse_enter", "mouse_leave"
};

static size_t next_pow2(size_t v) {
    size_t p = 16;
    while (p < v) p <<= 1;
    return p;
}

bool br_events_init(br_events *ev, size_t initial_capacity) {
    memset(ev, 0, sizeof(*ev));
    ev->capacity = next_pow2(initial_capacity ? initial_capacity : 256);
    ev->queue = BR_NEW_ARRAY(br_event, ev->capacity);
    ev->next_sub_id = 1;
    return ev->queue != NULL;
}

void br_events_destroy(br_events *ev) {
    uint32_t i;
    br_free(ev->queue);
    br_free(ev->subs);
    for (i = 0; i < ev->user_type_count; ++i) br_free(ev->type_names[i]);
    memset(ev, 0, sizeof(*ev));
}

uint32_t br_events_find_type(const br_events *ev, const char *name) {
    uint32_t i;
    for (i = 0; i < ev->user_type_count; ++i)
        if (strcmp(ev->type_names[i], name) == 0) return BR_EVENT_USER + i;
    for (i = 1; i < BR_EVENT_BUILTIN_COUNT; ++i)
        if (strcmp(k_builtin_names[i], name) == 0) return i;
    return 0;
}

uint32_t br_events_register_type(br_events *ev, const char *name) {
    uint32_t existing = br_events_find_type(ev, name);
    if (existing) return existing;
    if (ev->user_type_count >= BR_MAX_USER_EVENT_TYPES) {
        BR_ERROR("too many user event types (max %d)", BR_MAX_USER_EVENT_TYPES);
        return 0;
    }
    ev->type_names[ev->user_type_count] = br_strdup(name);
    return BR_EVENT_USER + ev->user_type_count++;
}

const char *br_events_type_name(const br_events *ev, uint32_t type) {
    if (type < BR_EVENT_BUILTIN_COUNT) return k_builtin_names[type];
    if (type >= BR_EVENT_USER && type - BR_EVENT_USER < ev->user_type_count)
        return ev->type_names[type - BR_EVENT_USER];
    return "unknown";
}

/* ---- subscriptions ------------------------------------------------------ */

static void sort_subs(br_events *ev) {
    /* Stable insertion sort by descending priority (lists are small). */
    size_t i, j;
    for (i = 1; i < ev->sub_count; ++i) {
        br_event_sub tmp = ev->subs[i];
        j = i;
        while (j > 0 && ev->subs[j - 1].priority < tmp.priority) {
            ev->subs[j] = ev->subs[j - 1];
            --j;
        }
        ev->subs[j] = tmp;
    }
}

static void finalize_subs(br_events *ev) {
    size_t r, w = 0;
    if (!ev->subs_dirty) return;
    for (r = 0; r < ev->sub_count; ++r) {
        if (!ev->subs[r].active) continue;
        ev->subs[r].pending = false;
        ev->subs[w++] = ev->subs[r];
    }
    ev->sub_count = w;
    sort_subs(ev);
    ev->subs_dirty = false;
}

br_subscription br_events_subscribe(br_events *ev, uint32_t type, br_event_handler fn,
                                    void *user, int priority) {
    br_event_sub *s;
    if (!fn) return 0;
    if (ev->sub_count == ev->sub_capacity) {
        size_t cap = ev->sub_capacity ? ev->sub_capacity * 2 : 16;
        br_event_sub *n = (br_event_sub *)br_realloc(ev->subs, cap * sizeof(br_event_sub));
        if (!n) return 0;
        ev->subs = n;
        ev->sub_capacity = cap;
    }
    s = &ev->subs[ev->sub_count++];
    s->id = ev->next_sub_id++;
    s->type = type;
    s->priority = priority;
    s->fn = fn;
    s->user = user;
    s->active = true;
    s->pending = ev->dispatch_depth > 0;
    ev->subs_dirty = true;
    if (ev->dispatch_depth == 0) finalize_subs(ev);
    return s->id;
}

void br_events_unsubscribe(br_events *ev, br_subscription id) {
    size_t i;
    for (i = 0; i < ev->sub_count; ++i) {
        if (ev->subs[i].id == id) {
            ev->subs[i].active = false;
            ev->subs_dirty = true;
            break;
        }
    }
    if (ev->dispatch_depth == 0) finalize_subs(ev);
}

/* ---- queue -------------------------------------------------------------- */

static bool grow_queue(br_events *ev) {
    size_t new_cap = ev->capacity * 2, i;
    br_event *n = BR_NEW_ARRAY(br_event, new_cap);
    if (!n) return false;
    for (i = 0; i < ev->count; ++i) n[i] = ev->queue[(ev->head + i) & (ev->capacity - 1)];
    br_free(ev->queue);
    ev->queue = n;
    ev->capacity = new_cap;
    ev->head = 0;
    return true;
}

bool br_events_post(br_events *ev, const br_event *e) {
    br_event *slot;
    if (ev->count == ev->capacity && !grow_queue(ev)) return false;
    slot = &ev->queue[(ev->head + ev->count) & (ev->capacity - 1)];
    *slot = *e;
    if (slot->timestamp == 0.0) slot->timestamp = ev->time;
    ev->count++;
    return true;
}

bool br_events_post_data(br_events *ev, uint32_t type, const void *data, size_t size) {
    br_event e = br_event_make(type);
    if (size > BR_EVENT_USER_DATA_SIZE) {
        BR_ERROR("event payload too large (%zu > %d)", size, BR_EVENT_USER_DATA_SIZE);
        return false;
    }
    if (data && size) memcpy(e.u.user.data, data, size);
    return br_events_post(ev, &e);
}

bool br_events_poll(br_events *ev, br_event *out) {
    if (ev->count == 0) return false;
    *out = ev->queue[ev->head];
    ev->head = (ev->head + 1) & (ev->capacity - 1);
    ev->count--;
    return true;
}

size_t br_events_pending(const br_events *ev) { return ev->count; }

br_event *br_events_peek(br_events *ev, size_t index) {
    if (index >= ev->count) return NULL;
    return &ev->queue[(ev->head + index) & (ev->capacity - 1)];
}
void br_events_clear(br_events *ev) { ev->head = 0; ev->count = 0; }

bool br_events_emit(br_events *ev, const br_event *e) {
    size_t i;
    bool consumed = false;
    ev->dispatch_depth++;
    /* Index loop: subs may be appended (realloc) by a handler. */
    for (i = 0; i < ev->sub_count; ++i) {
        br_event_sub s = ev->subs[i];
        if (!s.active || s.pending) continue;
        if (s.type != BR_EVENT_ANY && s.type != e->type) continue;
        if (s.fn(e, s.user)) { consumed = true; break; }
    }
    ev->dispatch_depth--;
    if (ev->dispatch_depth == 0) finalize_subs(ev);
    return consumed;
}

size_t br_events_dispatch(br_events *ev) {
    br_event e;
    size_t delivered = 0;
    while (br_events_poll(ev, &e)) {
        br_events_emit(ev, &e);
        if (++delivered >= BR_DISPATCH_SAFETY_LIMIT) {
            BR_WARN("event dispatch hit safety limit; %zu events deferred (feedback loop?)",
                    ev->count);
            break;
        }
    }
    return delivered;
}

/* ---- builders ----------------------------------------------------------- */

br_event br_event_make(uint32_t type) {
    br_event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    return e;
}

br_event br_event_key(uint32_t type, br_key key, uint32_t mods, bool repeat) {
    br_event e = br_event_make(type);
    e.u.key.key = key;
    e.u.key.mods = mods;
    e.u.key.repeat = repeat;
    return e;
}

br_event br_event_mouse_button(uint32_t type, br_mouse_button b, float x, float y) {
    br_event e = br_event_make(type);
    e.u.mouse_button.button = b;
    e.u.mouse_button.x = x;
    e.u.mouse_button.y = y;
    e.u.mouse_button.clicks = 1;
    e.u.mouse_button.wx = x;
    e.u.mouse_button.wy = y;
    return e;
}

br_event br_event_mouse_move(float x, float y, float dx, float dy) {
    br_event e = br_event_make(BR_EVENT_MOUSE_MOVE);
    e.u.mouse_move.x = x;
    e.u.mouse_move.y = y;
    e.u.mouse_move.dx = dx;
    e.u.mouse_move.dy = dy;
    e.u.mouse_move.wx = x;
    e.u.mouse_move.wy = y;
    return e;
}
