#include "bronto/input.h"
#include "bronto/event.h"

#include <math.h>
#include <string.h>

void br_input_init(br_input *in) {
    memset(in, 0, sizeof(*in));
    in->deadzone = 0.2f;
}

void br_input_begin_frame(br_input *in) {
    int i;
    memset(in->keys_pressed, 0, sizeof(in->keys_pressed));
    memset(in->keys_released, 0, sizeof(in->keys_released));
    memset(in->keys_repeated, 0, sizeof(in->keys_repeated));
    memset(in->mouse_pressed, 0, sizeof(in->mouse_pressed));
    memset(in->mouse_released, 0, sizeof(in->mouse_released));
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) {
        memset(in->pads[i].pressed, 0, sizeof(in->pads[i].pressed));
        memset(in->pads[i].released, 0, sizeof(in->pads[i].released));
        memset(in->pads[i].trigger_pressed, 0, sizeof(in->pads[i].trigger_pressed));
        memset(in->pads[i].trigger_released, 0, sizeof(in->pads[i].trigger_released));
    }
    in->mouse_dx = in->mouse_dy = 0;
    in->wheel_x = in->wheel_y = 0;
    in->text_count = 0;
    for (i = 0; i < in->action_count; ++i) in->actions[i].prev_down = in->actions[i].down;
}

void br_input_reset(br_input *in) {
    int i, b;
    for (i = 0; i < BR_KEY_COUNT; ++i)
        if (in->keys[i]) { in->keys[i] = false; in->keys_released[i] = true; }
    for (i = 0; i < BR_MOUSE_BUTTON_COUNT; ++i)
        if (in->mouse[i]) { in->mouse[i] = false; in->mouse_released[i] = true; }
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) {
        for (b = 0; b < BR_PAD_BUTTON_COUNT; ++b)
            if (in->pads[i].buttons[b]) { in->pads[i].buttons[b] = false; in->pads[i].released[b] = true; }
        for (b = 0; b < 2; ++b)
            if (in->pads[i].triggers[b]) { in->pads[i].triggers[b] = false; in->pads[i].trigger_released[b] = true; }
        memset(in->pads[i].axes, 0, sizeof(in->pads[i].axes));
    }
    in->mods = 0;
}

static bool valid_key(int k) { return k > 0 && k < BR_KEY_COUNT; }
static bool valid_pad(int p) { return p >= 0 && p < BR_MAX_GAMEPADS; }

void br_input_handle_event(br_input *in, const br_event *e) {
    switch (e->type) {
    case BR_EVENT_KEY_DOWN: {
        int k = (int)e->u.key.key;
        in->mods = e->u.key.mods;
        if (!valid_key(k)) break;
        in->last_device = BR_DEVICE_KEYBOARD;
        if (!in->keys[k]) in->keys_pressed[k] = true;
        in->keys_repeated[k] = true;
        in->keys[k] = true;
        break;
    }
    case BR_EVENT_KEY_UP: {
        int k = (int)e->u.key.key;
        in->mods = e->u.key.mods;
        if (!valid_key(k)) break;
        if (in->keys[k]) in->keys_released[k] = true;
        in->keys[k] = false;
        break;
    }
    case BR_EVENT_TEXT_INPUT:
        if (in->text_count < BR_TEXT_INPUT_MAX) in->text[in->text_count++] = e->u.text.codepoint;
        break;
    case BR_EVENT_MOUSE_MOVE:
        in->mouse_dx += e->u.mouse_move.x - in->mouse_x;
        in->mouse_dy += e->u.mouse_move.y - in->mouse_y;
        in->mouse_x = e->u.mouse_move.x;
        in->mouse_y = e->u.mouse_move.y;
        if (fabsf(e->u.mouse_move.dx) + fabsf(e->u.mouse_move.dy) >= 3) in->last_device = BR_DEVICE_MOUSE;   /* (not a nudge) */
        break;
    case BR_EVENT_MOUSE_BUTTON_DOWN: {
        int b = (int)e->u.mouse_button.button;
        if (b < 0 || b >= BR_MOUSE_BUTTON_COUNT) break;
        if (!in->mouse[b]) in->mouse_pressed[b] = true;
        in->mouse[b] = true;
        in->last_device = BR_DEVICE_MOUSE;
        in->mouse_x = e->u.mouse_button.x;
        in->mouse_y = e->u.mouse_button.y;
        break;
    }
    case BR_EVENT_MOUSE_BUTTON_UP: {
        int b = (int)e->u.mouse_button.button;
        if (b < 0 || b >= BR_MOUSE_BUTTON_COUNT) break;
        if (in->mouse[b]) in->mouse_released[b] = true;
        in->mouse[b] = false;
        in->mouse_x = e->u.mouse_button.x;
        in->mouse_y = e->u.mouse_button.y;
        break;
    }
    case BR_EVENT_MOUSE_WHEEL:
        in->wheel_x += e->u.wheel.dx;
        in->wheel_y += e->u.wheel.dy;
        in->last_device = BR_DEVICE_MOUSE;
        break;
    case BR_EVENT_GAMEPAD_CONNECTED:
        if (valid_pad(e->u.gamepad.pad)) {
            br_gamepad_state *p = &in->pads[e->u.gamepad.pad];
            p->connected = true;
            memset(p->name, 0, sizeof(p->name));
            if (e->u.gamepad.name) strncpy(p->name, e->u.gamepad.name, sizeof(p->name) - 1);
        }
        break;
    case BR_EVENT_GAMEPAD_DISCONNECTED:
        if (valid_pad(e->u.gamepad.pad)) {
            br_gamepad_state *p = &in->pads[e->u.gamepad.pad];
            int b;
            for (b = 0; b < BR_PAD_BUTTON_COUNT; ++b)
                if (p->buttons[b]) { p->buttons[b] = false; p->released[b] = true; }
            memset(p->axes, 0, sizeof(p->axes));
            p->connected = false;
            p->name[0] = 0;
        }
        break;
    case BR_EVENT_GAMEPAD_BUTTON_DOWN:
    case BR_EVENT_GAMEPAD_BUTTON_UP: {
        int pad = e->u.gamepad.pad, b = e->u.gamepad.button;
        bool down = e->type == BR_EVENT_GAMEPAD_BUTTON_DOWN;
        br_gamepad_state *p;
        if (!valid_pad(pad) || b < 0 || b >= BR_PAD_BUTTON_COUNT) break;
        p = &in->pads[pad];
        p->connected = true;
        if (down && !p->buttons[b]) p->pressed[b] = true;
        if (!down && p->buttons[b]) p->released[b] = true;
        p->buttons[b] = down;
        if (down) in->last_device = BR_DEVICE_GAMEPAD;
        break;
    }
    case BR_EVENT_GAMEPAD_AXIS: {
        int pad = e->u.gamepad.pad, a = e->u.gamepad.axis;
        if (!valid_pad(pad) || a < 0 || a >= BR_PAD_AXIS_COUNT) break;
        in->pads[pad].connected = true;
        in->pads[pad].axes[a] = e->u.gamepad.value;
        if (fabsf(e->u.gamepad.value) >= 0.5f) in->last_device = BR_DEVICE_GAMEPAD;   /* (pushed, not drifting) */
        break;
    }
    case BR_EVENT_WINDOW_FOCUS_LOST:
        br_input_reset(in);
        break;
    default:
        break;
    }
}

/* ---- queries ------------------------------------------------------------ */

bool br_key_down(const br_input *in, br_key k)     { return valid_key((int)k) && in->keys[k]; }
bool br_key_pressed(const br_input *in, br_key k)  { return valid_key((int)k) && in->keys_pressed[k]; }
bool br_key_released(const br_input *in, br_key k) { return valid_key((int)k) && in->keys_released[k]; }
bool br_key_repeated(const br_input *in, br_key k) { return valid_key((int)k) && in->keys_repeated[k]; }

static bool valid_mb(br_mouse_button b) { return (int)b >= 0 && b < BR_MOUSE_BUTTON_COUNT; }
bool br_mouse_down(const br_input *in, br_mouse_button b)     { return valid_mb(b) && in->mouse[b]; }
bool br_mouse_pressed(const br_input *in, br_mouse_button b)  { return valid_mb(b) && in->mouse_pressed[b]; }
bool br_mouse_released(const br_input *in, br_mouse_button b) { return valid_mb(b) && in->mouse_released[b]; }

bool br_pad_connected(const br_input *in, int pad) { return valid_pad(pad) && in->pads[pad].connected; }
bool br_pad_down(const br_input *in, int pad, br_gamepad_button b) {
    return valid_pad(pad) && (int)b >= 0 && b < BR_PAD_BUTTON_COUNT && in->pads[pad].buttons[b];
}
bool br_pad_pressed(const br_input *in, int pad, br_gamepad_button b) {
    return valid_pad(pad) && (int)b >= 0 && b < BR_PAD_BUTTON_COUNT && in->pads[pad].pressed[b];
}
/* ---- a frame's edges, on their own ---------------------------------------- */

void br_input_edges_get(const br_input *in, br_input_edges *out) {
    int i;
    memcpy(out->keys_pressed, in->keys_pressed, sizeof(out->keys_pressed));
    memcpy(out->keys_released, in->keys_released, sizeof(out->keys_released));
    memcpy(out->keys_repeated, in->keys_repeated, sizeof(out->keys_repeated));
    memcpy(out->mouse_pressed, in->mouse_pressed, sizeof(out->mouse_pressed));
    memcpy(out->mouse_released, in->mouse_released, sizeof(out->mouse_released));
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) {
        memcpy(out->pads[i].pressed, in->pads[i].pressed, sizeof(out->pads[i].pressed));
        memcpy(out->pads[i].released, in->pads[i].released, sizeof(out->pads[i].released));
        memcpy(out->pads[i].trigger_pressed, in->pads[i].trigger_pressed, sizeof(out->pads[i].trigger_pressed));
        memcpy(out->pads[i].trigger_released, in->pads[i].trigger_released, sizeof(out->pads[i].trigger_released));
    }
}

void br_input_edges_set(br_input *in, const br_input_edges *ed) {
    int i;
    memcpy(in->keys_pressed, ed->keys_pressed, sizeof(in->keys_pressed));
    memcpy(in->keys_released, ed->keys_released, sizeof(in->keys_released));
    memcpy(in->keys_repeated, ed->keys_repeated, sizeof(in->keys_repeated));
    memcpy(in->mouse_pressed, ed->mouse_pressed, sizeof(in->mouse_pressed));
    memcpy(in->mouse_released, ed->mouse_released, sizeof(in->mouse_released));
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) {
        memcpy(in->pads[i].pressed, ed->pads[i].pressed, sizeof(in->pads[i].pressed));
        memcpy(in->pads[i].released, ed->pads[i].released, sizeof(in->pads[i].released));
        memcpy(in->pads[i].trigger_pressed, ed->pads[i].trigger_pressed, sizeof(in->pads[i].trigger_pressed));
        memcpy(in->pads[i].trigger_released, ed->pads[i].trigger_released, sizeof(in->pads[i].trigger_released));
    }
}

void br_input_edges_add(br_input_edges *to, const br_input_edges *from) {
    const bool *f = (const bool *)from;                 /* (nothing but bools) */
    bool *t = (bool *)to;
    size_t i;
    for (i = 0; i < sizeof(*to) / sizeof(bool); ++i) t[i] = t[i] || f[i];
}

void br_input_edges_clear(br_input *in) {
    static const br_input_edges none;
    br_input_edges_set(in, &none);
}

const char *br_input_device_name(br_input_device d) {
    switch (d) {
    case BR_DEVICE_KEYBOARD: return "keyboard";
    case BR_DEVICE_MOUSE: return "mouse";
    case BR_DEVICE_GAMEPAD: return "gamepad";
    default: return "";
    }
}

bool br_pad_released(const br_input *in, int pad, br_gamepad_button b) {
    return valid_pad(pad) && (int)b >= 0 && b < BR_PAD_BUTTON_COUNT && in->pads[pad].released[b];
}
float br_pad_axis(const br_input *in, int pad, br_gamepad_axis a) {
    float v, dz;
    if (!valid_pad(pad) || (int)a < 0 || a >= BR_PAD_AXIS_COUNT) return 0;
    v = in->pads[pad].axes[a];
    dz = in->deadzone;
    if (v > -dz && v < dz) return 0;
    /* Rescale so output starts at 0 just outside the deadzone. */
    return v > 0 ? (v - dz) / (1.0f - dz) : (v + dz) / (1.0f - dz);
}

/* ---- actions ------------------------------------------------------------ */

br_action br_input_find_action(const br_input *in, const char *name) {
    int i;
    for (i = 0; i < in->action_count; ++i)
        if (strcmp(in->actions[i].name, name) == 0) return i;
    return -1;
}

br_action br_input_action(br_input *in, const char *name) {
    br_action a = br_input_find_action(in, name);
    br_action_def *d;
    if (a >= 0) return a;
    if (in->action_count >= BR_MAX_ACTIONS) return -1;
    d = &in->actions[in->action_count];
    memset(d, 0, sizeof(*d));
    strncpy(d->name, name, BR_ACTION_NAME_MAX - 1);
    return in->action_count++;
}

bool br_input_bind(br_input *in, br_action a, br_binding_kind kind, int code, int pad) {
    br_action_def *d;
    if (a < 0 || a >= in->action_count) return false;
    d = &in->actions[a];
    if (d->binding_count >= BR_MAX_ACTION_BINDINGS) return false;
    d->bindings[d->binding_count].kind = kind;
    d->bindings[d->binding_count].code = code;
    d->bindings[d->binding_count].pad = pad;
    d->binding_count++;
    return true;
}

void br_input_clear_bindings(br_input *in, br_action a) {
    if (a >= 0 && a < in->action_count) in->actions[a].binding_count = 0;
}

static bool binding_down_on_pad(const br_input *in, const br_binding *b, int pad) {
    const br_gamepad_state *p = &in->pads[pad];
    switch (b->kind) {
    case BR_BIND_PAD_BUTTON:
        return b->code >= 0 && b->code < BR_PAD_BUTTON_COUNT && p->buttons[b->code];
    case BR_BIND_PAD_AXIS_POS:
        return b->code >= 0 && b->code < BR_PAD_AXIS_COUNT && p->axes[b->code] > in->deadzone;
    case BR_BIND_PAD_AXIS_NEG:
        return b->code >= 0 && b->code < BR_PAD_AXIS_COUNT && p->axes[b->code] < -in->deadzone;
    default:
        return false;
    }
}

static bool binding_down(const br_input *in, const br_binding *b) {
    int p;
    switch (b->kind) {
    case BR_BIND_KEY:
        return valid_key(b->code) && in->keys[b->code];
    case BR_BIND_MOUSE_BUTTON:
        return b->code >= 0 && b->code < BR_MOUSE_BUTTON_COUNT && in->mouse[b->code];
    case BR_BIND_PAD_BUTTON:
    case BR_BIND_PAD_AXIS_POS:
    case BR_BIND_PAD_AXIS_NEG:
        if (b->pad >= 0) return valid_pad(b->pad) && binding_down_on_pad(in, b, b->pad);
        for (p = 0; p < BR_MAX_GAMEPADS; ++p)
            if (binding_down_on_pad(in, b, p)) return true;
        return false;
    default:
        return false;
    }
}

/* A binding that was pressed AND released within one frame still counts as
 * a press for the action, so taps are never lost. */
static bool binding_tapped(const br_input *in, const br_binding *b) {
    int p;
    switch (b->kind) {
    case BR_BIND_KEY:
        return valid_key(b->code) && in->keys_pressed[b->code];
    case BR_BIND_MOUSE_BUTTON:
        return b->code >= 0 && b->code < BR_MOUSE_BUTTON_COUNT && in->mouse_pressed[b->code];
    case BR_BIND_PAD_BUTTON:
        if (b->code < 0 || b->code >= BR_PAD_BUTTON_COUNT) return false;
        for (p = 0; p < BR_MAX_GAMEPADS; ++p)
            if ((b->pad < 0 || b->pad == p) && in->pads[p].pressed[b->code]) return true;
        return false;
    default:
        return false;
    }
}

void br_input_end_events(br_input *in) {
    int i, j;
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) {                /* the triggers as buttons */
        br_gamepad_state *p = &in->pads[i];
        for (j = 0; j < 2; ++j) {
            float v = p->connected ? p->axes[BR_PAD_AXIS_LEFT_TRIGGER + j] : 0;
            if (!p->triggers[j] && v >= 0.5f) { p->triggers[j] = true; p->trigger_pressed[j] = true; }
            else if (p->triggers[j] && v < 0.4f) { p->triggers[j] = false; p->trigger_released[j] = true; }
        }
    }
    for (i = 0; i < in->action_count; ++i) {
        br_action_def *d = &in->actions[i];
        bool down = false, tapped = false;
        for (j = 0; j < d->binding_count; ++j) {
            if (binding_down(in, &d->bindings[j])) down = true;
            if (binding_tapped(in, &d->bindings[j])) tapped = true;
        }
        /* A tap that already ended still reports pressed this frame: model it
         * as prev_down=false, down=true for the edge, then the next frame sees
         * the real (released) state. */
        if (tapped && !down && !d->prev_down) {
            d->down = true;      /* visible as pressed for this frame */
        } else {
            d->down = down;
        }
    }
}

bool br_action_down(const br_input *in, br_action a) {
    return a >= 0 && a < in->action_count && in->actions[a].down;
}
bool br_action_pressed(const br_input *in, br_action a) {
    return a >= 0 && a < in->action_count && in->actions[a].down && !in->actions[a].prev_down;
}
bool br_action_released(const br_input *in, br_action a) {
    return a >= 0 && a < in->action_count && !in->actions[a].down && in->actions[a].prev_down;
}
float br_action_axis(const br_input *in, br_action negative, br_action positive) {
    return (br_action_down(in, positive) ? 1.0f : 0.0f) - (br_action_down(in, negative) ? 1.0f : 0.0f);
}

const char *br_pad_name(const br_input *in, int pad) {
    return valid_pad(pad) && in->pads[pad].connected ? in->pads[pad].name : "";
}

/* ---- gamepad snapshot diffing ------------------------------------------ */

#define AXIS_EPSILON (1.0f / 512.0f)

static void post_pad(br_events *ev, uint32_t type, int pad, int button, int axis, float value, const char *name) {
    br_event e = br_event_make(type);
    e.u.gamepad.pad = pad;
    e.u.gamepad.button = button;
    e.u.gamepad.axis = axis;
    e.u.gamepad.value = value;
    e.u.gamepad.name = name;
    br_events_post(ev, &e);
}

void br_gamepads_sync(br_gamepad_tracker *t, int pad, const br_gamepad_raw *now, br_events *ev) {
    br_gamepad_raw *prev;
    int b, a;
    if (!valid_pad(pad)) return;
    prev = &t->prev[pad];
    if (!now->connected) {
        if (!prev->connected) return;
        /* release everything first so nothing stays stuck "held" */
        for (b = 0; b < BR_PAD_BUTTON_COUNT; ++b)
            if (prev->buttons & (1u << b)) post_pad(ev, BR_EVENT_GAMEPAD_BUTTON_UP, pad, b, 0, 0, NULL);
        for (a = 0; a < BR_PAD_AXIS_COUNT; ++a)
            if (prev->axes[a] != 0.0f) post_pad(ev, BR_EVENT_GAMEPAD_AXIS, pad, 0, a, 0, NULL);
        post_pad(ev, BR_EVENT_GAMEPAD_DISCONNECTED, pad, 0, 0, 0, NULL);
        memset(prev, 0, sizeof(*prev));
        return;
    }
    if (!prev->connected) {
        memset(prev, 0, sizeof(*prev));
        prev->connected = true;
        memcpy(prev->name, now->name, sizeof(prev->name));
        prev->name[BR_GAMEPAD_NAME_MAX - 1] = 0;
        post_pad(ev, BR_EVENT_GAMEPAD_CONNECTED, pad, 0, 0, 0, prev->name);
    }
    for (b = 0; b < BR_PAD_BUTTON_COUNT; ++b) {
        uint32_t bit = 1u << b;
        if ((now->buttons & bit) != (prev->buttons & bit))
            post_pad(ev, (now->buttons & bit) ? BR_EVENT_GAMEPAD_BUTTON_DOWN : BR_EVENT_GAMEPAD_BUTTON_UP, pad, b, 0, 0, NULL);
    }
    prev->buttons = now->buttons & ((1u << BR_PAD_BUTTON_COUNT) - 1u);
    for (a = 0; a < BR_PAD_AXIS_COUNT; ++a) {
        float lo = a >= BR_PAD_AXIS_LEFT_TRIGGER ? 0.0f : -1.0f;
        float v = now->axes[a] < lo ? lo : (now->axes[a] > 1.0f ? 1.0f : now->axes[a]);
        float d = v - prev->axes[a];
        /* report real movement, and always report reaching exactly rest */
        if (d > AXIS_EPSILON || d < -AXIS_EPSILON || (v == 0.0f && prev->axes[a] != 0.0f)) {
            post_pad(ev, BR_EVENT_GAMEPAD_AXIS, pad, 0, a, v, NULL);
            prev->axes[a] = v;
        }
    }
}

/* ---- names -------------------------------------------------------------- */

const char *br_key_name(br_key k) {
    static char ascii[2];
    switch (k) {
    case BR_KEY_SPACE: return "Space";
    case BR_KEY_ESCAPE: return "Escape";
    case BR_KEY_ENTER: return "Enter";
    case BR_KEY_TAB: return "Tab";
    case BR_KEY_BACKSPACE: return "Backspace";
    case BR_KEY_INSERT: return "Insert";
    case BR_KEY_DELETE: return "Delete";
    case BR_KEY_RIGHT: return "Right";
    case BR_KEY_LEFT: return "Left";
    case BR_KEY_DOWN: return "Down";
    case BR_KEY_UP: return "Up";
    case BR_KEY_PAGE_UP: return "PageUp";
    case BR_KEY_PAGE_DOWN: return "PageDown";
    case BR_KEY_HOME: return "Home";
    case BR_KEY_END: return "End";
    case BR_KEY_LEFT_SHIFT: return "LeftShift";
    case BR_KEY_RIGHT_SHIFT: return "RightShift";
    case BR_KEY_LEFT_CONTROL: return "LeftCtrl";
    case BR_KEY_RIGHT_CONTROL: return "RightCtrl";
    case BR_KEY_LEFT_ALT: return "LeftAlt";
    case BR_KEY_RIGHT_ALT: return "RightAlt";
    case BR_KEY_LEFT_SUPER: return "LeftSuper";
    case BR_KEY_RIGHT_SUPER: return "RightSuper";
    default: break;
    }
    if (k >= BR_KEY_F1 && k <= BR_KEY_F12) {
        static const char *f[] = { "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12" };
        return f[k - BR_KEY_F1];
    }
    if (k > 32 && k < 127) { ascii[0] = (char)k; ascii[1] = 0; return ascii; }
    return "Unknown";
}
