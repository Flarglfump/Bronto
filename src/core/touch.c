/*
 * Bronto Engine - on-screen touch controls (see touch.h).
 *
 * Every frame the engine places the controls for the window
 * (br_touch_ui_layout), then hands the frame's events here before anyone
 * sees them (br_touch_ui_process): touches on a control work it, others
 * become the mouse. What the controls hold is then posted as a virtual
 * gamepad's events (or key events), exactly as a real pad's would be.
 */
#include "bronto/touch.h"
#include "bronto/event.h"
#include "bronto/render.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TOUCH_LT BR_PAD_BUTTON_COUNT            /* br_touch_control.button for the triggers */
#define TOUCH_RT (BR_PAD_BUTTON_COUNT + 1)
#define TOUCH_BUTTON_KINDS (BR_PAD_BUTTON_COUNT + 2)
#define MOUSE_FINGER 0x7ffffff0                 /* the touch id the mouse plays (mouse_as_touch) */
#define FINGER_MOUSE (-1)                       /* a finger that works the mouse */
#define FINGER_NONE (-2)                        /* ... that works nothing */

static const char *const tc_k_names[TOUCH_BUTTON_KINDS] = {
    "a", "b", "x", "y", "lb", "rb", "back", "start", "guide", "ls", "rs", "up", "right", "down", "left", "lt", "rt"
};
static const char *const tc_k_labels[TOUCH_BUTTON_KINDS] = {
    "A", "B", "X", "Y", "LB", "RB", "BACK", "START", "HOME", "LS", "RS", "^", ">", "v", "<", "LT", "RT"
};

void br_touch_ui_init(br_touch_ui *t) {
    memset(t, 0, sizeof(*t));
    t->mode = BR_TOUCH_AUTO;
    t->pad = -1;
    t->scale = 1;
}

/* ---- the list --------------------------------------------------------------------- */

static bool tc_name_eq(const char *a, size_t n, const char *b) {
    size_t i;
    for (i = 0; i < n; ++i) if (!b[i] || tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return b[n] == 0;
}

/* A key by the names scripts use ("space", "z", "enter", "left"). */
static br_key tc_key_named(const char *s, size_t n) {
    static const struct { const char *alias; br_key k; } aliases[] = {
        { "space", BR_KEY_SPACE }, { "enter", BR_KEY_ENTER }, { "return", BR_KEY_ENTER }, { "escape", BR_KEY_ESCAPE },
        { "esc", BR_KEY_ESCAPE }, { "shift", BR_KEY_LEFT_SHIFT }, { "ctrl", BR_KEY_LEFT_CONTROL }, { "alt", BR_KEY_LEFT_ALT },
        { "tab", BR_KEY_TAB }, { "backspace", BR_KEY_BACKSPACE },
    };
    size_t i;
    int k;
    for (i = 0; i < BR_ARRAY_COUNT(aliases); ++i) if (tc_name_eq(s, n, aliases[i].alias)) return aliases[i].k;
    for (k = 1; k < BR_KEY_COUNT; ++k) {
        const char *kn = br_key_name((br_key)k);
        if (kn && kn[0] && tc_name_eq(s, n, kn)) return (br_key)k;
    }
    return BR_KEY_UNKNOWN;
}

static void tc_trim(const char **s, const char **e) {
    while (*s < *e && isspace((unsigned char)**s)) ++*s;
    while (*e > *s && isspace((unsigned char)(*e)[-1])) --*e;
}

static void tc_release_all(br_touch_ui *t);

bool br_touch_ui_configure(br_touch_ui *t, const char *spec, char *error, size_t error_size) {
    br_touch_control list[BR_MAX_TOUCH_CONTROLS];
    int n = 0;
    const char *p = spec ? spec : "";
    if (error && error_size) error[0] = 0;
    memset(list, 0, sizeof(list));
    while (*p) {
        const char *end = p, *name, *name_end, *key = NULL, *key_end = NULL, *label = NULL, *label_end = NULL, *q;
        br_touch_control *c;
        int b, k;
        while (*end && *end != ',') ++end;
        name = p;
        name_end = end;
        for (q = p; q < end; ++q) {                              /* name[=key][:label] */
            if (*q == ':' && !label) { label = q + 1; label_end = end; if (!key) name_end = q; else key_end = q; break; }
            if (*q == '=' && !key) { key = q + 1; key_end = end; name_end = q; }
        }
        tc_trim(&name, &name_end);
        p = *end ? end + 1 : end;
        if (name == name_end) continue;                          /* (an empty item: "a,,b" or a trailing comma) */
        if (n == BR_MAX_TOUCH_CONTROLS) { if (error) snprintf(error, error_size, "too many touch controls (at most %d)", BR_MAX_TOUCH_CONTROLS); return false; }
        c = &list[n];
        if (tc_name_eq(name, (size_t)(name_end - name), "stick")) b = BR_TOUCH_STICK;
        else {
            for (b = 0; b < TOUCH_BUTTON_KINDS && !tc_name_eq(name, (size_t)(name_end - name), tc_k_names[b]); ++b) {}
            if (b == TOUCH_BUTTON_KINDS) {
                if (error) snprintf(error, error_size, "unknown touch control '%.*s' (stick, a b x y, lb rb lt rt, back start, up down left right)", (int)(name_end - name), name);
                return false;
            }
        }
        for (k = 0; k < n; ++k)
            if (list[k].button == b) { if (error) snprintf(error, error_size, "the touch control '%.*s' is listed twice", (int)(name_end - name), name); return false; }
        c->button = b;
        c->finger = -1;
        if (key) {
            tc_trim(&key, &key_end);
            if (b == BR_TOUCH_STICK) {
                if (tc_name_eq(key, (size_t)(key_end - key), "arrows")) { c->keys[0] = BR_KEY_UP; c->keys[1] = BR_KEY_DOWN; c->keys[2] = BR_KEY_LEFT; c->keys[3] = BR_KEY_RIGHT; }
                else if (tc_name_eq(key, (size_t)(key_end - key), "wasd")) { c->keys[0] = BR_KEY_W; c->keys[1] = BR_KEY_S; c->keys[2] = BR_KEY_A; c->keys[3] = BR_KEY_D; }
                else { if (error) snprintf(error, error_size, "the touch stick presses 'arrows' or 'wasd', not '%.*s'", (int)(key_end - key), key); return false; }
            } else if ((c->keys[0] = tc_key_named(key, (size_t)(key_end - key))) == BR_KEY_UNKNOWN) {
                if (error) snprintf(error, error_size, "unknown key '%.*s' for the touch control '%s'", (int)(key_end - key), key, tc_k_names[b]);
                return false;
            }
        }
        if (label) {
            tc_trim(&label, &label_end);
            snprintf(c->label, sizeof(c->label), "%.*s", (int)(label_end - label), label);
        } else if (b != BR_TOUCH_STICK) {
            snprintf(c->label, sizeof(c->label), "%s", tc_k_labels[b]);
        }
        n++;
    }
    tc_release_all(t);
    memcpy(t->controls, list, sizeof(list));
    t->count = n;
    br_touch_ui_layout(t, t->win_w, t->win_h, t->scale, t->viewport);
    return true;
}

void br_touch_ui_set_mode(br_touch_ui *t, br_touch_mode mode) {
    t->mode = mode;
    if (mode == BR_TOUCH_OFF) { t->shown = false; tc_release_all(t); }
    if (mode == BR_TOUCH_ALWAYS) t->shown = true;
}

void br_touch_ui_hide(br_touch_ui *t, bool hidden) {
    t->hidden = hidden;
    if (hidden) tc_release_all(t);
}

bool br_touch_ui_visible(const br_touch_ui *t) {
    return t->count > 0 && !t->hidden && t->mode != BR_TOUCH_OFF && (t->shown || t->mode == BR_TOUCH_ALWAYS);
}

/* ---- where they go ------------------------------------------------------------------ */

static float tc_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static bool tc_tall(int w, int h) { return h > w + w / 8; }

/* The sizes everything is laid out with (points): margin, stick radius, button radius, diamond spacing. */
static void tc_metrics(float W, float H, float *m, float *R, float *b, float *d) {
    float least = W < H ? W : H;
    *m = 12;
    *R = tc_clampf(least * 0.17f, 40, 78);
    *b = tc_clampf(least * 0.075f, 20, 32);
    *d = *b * 1.9f;
}

void br_touch_ui_place_viewport(const br_touch_ui *t, int win_w, int win_h, float scale, br_viewport *vp) {
    float s = scale > 0 ? scale : 1, m, R, b, d, side;
    if (!br_touch_ui_visible(t) || vp->rect.w <= 0 || vp->rect.h <= 0) return;
    if (tc_tall(win_w, win_h)) {
        vp->rect.y = (int)(12 * s);                              /* the game at the top, the controls below it */
        if (vp->rect.y + vp->rect.h > win_h) vp->rect.y = win_h - vp->rect.h;
        if (vp->rect.y < 0) vp->rect.y = 0;
        return;
    }
    tc_metrics((float)win_w / s, (float)win_h / s, &m, &R, &b, &d);   /* wide: room beside the game for them */
    side = (m + (2.2f * R > 2 * d + 2 * b ? 2.2f * R : 2 * d + 2 * b) + 6) * s;
    if ((float)(win_w - vp->rect.w) / 2 < side) {                /* (a tablet: the game gives up some room, down to 60%) */
        float w = (float)win_w - 2 * side, min_w = (float)vp->rect.w * 0.6f, k;
        if (w < min_w) w = min_w;
        k = w / (float)vp->rect.w;
        vp->scale_x *= k;
        vp->scale_y *= k;
        vp->rect.w = (int)((float)vp->rect.w * k + 0.5f);
        vp->rect.h = (int)((float)vp->rect.h * k + 0.5f);
        vp->rect.x = (win_w - vp->rect.w) / 2;
        vp->rect.y = (win_h - vp->rect.h) / 2;
    }
}

static br_touch_control *tc_find_button(br_touch_ui *t, int button) {
    int i;
    for (i = 0; i < t->count; ++i) if (t->controls[i].button == button) return &t->controls[i];
    return NULL;
}

static void tc_place(br_touch_control *c, float x, float y, float w, float h, bool round, float s) {
    if (!c) return;
    c->x = x * s;
    c->y = y * s;
    c->w = w * s;
    c->h = h * s;
    c->round = round;
}

static void tc_build_items(br_touch_ui *t);

/* All in points (window pixels / scale) until tc_place() scales them. */
void br_touch_ui_layout(br_touch_ui *t, int win_w, int win_h, float scale, br_recti viewport) {
    float s = scale > 0 ? scale : 1, W = (float)win_w / s, H = (float)win_h / s, m, R, b, d;
    float top, bottom, sx, sy, fx, fy, row;
    bool portrait = tc_tall(win_w, win_h), has_stick = tc_find_button(t, BR_TOUCH_STICK) != NULL;
    int shoulders_l = (tc_find_button(t, BR_PAD_LEFT_BUMPER) ? 1 : 0) + (tc_find_button(t, TOUCH_LT) ? 1 : 0);
    int shoulders_r = (tc_find_button(t, BR_PAD_RIGHT_BUMPER) ? 1 : 0) + (tc_find_button(t, TOUCH_RT) ? 1 : 0);
    t->win_w = win_w;
    t->win_h = win_h;
    t->scale = s;
    t->viewport = viewport;
    if (W <= 0 || H <= 0) { t->item_count = 0; return; }
    tc_metrics(W, H, &m, &R, &b, &d);
    top = m;
    bottom = H - m;
    if (portrait && br_touch_ui_visible(t)) {                   /* below the game: the top row just above the stick */
        float below = (float)(viewport.y + viewport.h) / s + 8;  /*  and the buttons, where thumbs reach, if there's room */
        float above_sticks = bottom - (2.2f * R > 2 * d + 2 * b ? 2.2f * R : 2 * d + 2 * b) - 2.4f * b;
        top = above_sticks > below ? above_sticks : below;
    }
    if (top > bottom - 2 * R) top = bottom - 2 * R;

    sx = m + R * 1.1f;                                           /* the stick: bottom left */
    sy = bottom - R * 1.1f;
    tc_place(tc_find_button(t, BR_TOUCH_STICK), sx, sy, 2 * R, 2 * R, true, s);
    {                                                            /* the d-pad: there too (above the stick if both) */
        float px = sx, py = has_stick ? sy - R * 1.1f - b * 2.8f : sy, k = b * 1.55f;
        tc_place(tc_find_button(t, BR_PAD_DPAD_UP), px, py - k, k, k, false, s);
        tc_place(tc_find_button(t, BR_PAD_DPAD_DOWN), px, py + k, k, k, false, s);
        tc_place(tc_find_button(t, BR_PAD_DPAD_LEFT), px - k, py, k, k, false, s);
        tc_place(tc_find_button(t, BR_PAD_DPAD_RIGHT), px + k, py, k, k, false, s);
    }
    fx = W - m - d - b;                                          /* A B X Y: a diamond at the bottom right */
    fy = bottom - d - b;
    tc_place(tc_find_button(t, BR_PAD_A), fx, fy + d, 2 * b, 2 * b, true, s);
    tc_place(tc_find_button(t, BR_PAD_B), fx + d, fy, 2 * b, 2 * b, true, s);
    tc_place(tc_find_button(t, BR_PAD_X), fx - d, fy, 2 * b, 2 * b, true, s);
    tc_place(tc_find_button(t, BR_PAD_Y), fx, fy - d, 2 * b, 2 * b, true, s);
    tc_place(tc_find_button(t, BR_PAD_LEFT_STICK), sx + R * 1.05f, sy - R * 1.05f, 1.6f * b, 1.6f * b, true, s);
    tc_place(tc_find_button(t, BR_PAD_RIGHT_STICK), fx - d - b * 1.3f, fy - d - b * 0.3f, 1.6f * b, 1.6f * b, true, s);

    row = b * 1.75f;                                             /* the shoulders: the top corners */
    tc_place(tc_find_button(t, BR_PAD_LEFT_BUMPER), m + 1.4f * b, top + 0.7f * b, 2.8f * b, 1.4f * b, false, s);
    tc_place(tc_find_button(t, TOUCH_LT), m + 1.4f * b, top + 0.7f * b + (tc_find_button(t, BR_PAD_LEFT_BUMPER) ? row : 0), 2.8f * b, 1.4f * b, false, s);
    tc_place(tc_find_button(t, BR_PAD_RIGHT_BUMPER), W - m - 1.4f * b, top + 0.7f * b, 2.8f * b, 1.4f * b, false, s);
    tc_place(tc_find_button(t, TOUCH_RT), W - m - 1.4f * b, top + 0.7f * b + (tc_find_button(t, BR_PAD_RIGHT_BUMPER) ? row : 0), 2.8f * b, 1.4f * b, false, s);

    if (portrait) {                                              /* BACK, START (and HOME): between the shoulders, */
        float y = top + 0.6f * b;                                /*  or under them at the sides */
        tc_place(tc_find_button(t, BR_PAD_BACK), W / 2 - 1.5f * b, y, 2.6f * b, 1.2f * b, false, s);
        tc_place(tc_find_button(t, BR_PAD_START), W / 2 + 1.5f * b, y, 2.6f * b, 1.2f * b, false, s);
        tc_place(tc_find_button(t, BR_PAD_GUIDE), W / 2, y + 1.6f * b, 2.6f * b, 1.2f * b, false, s);
    } else {
        tc_place(tc_find_button(t, BR_PAD_BACK), m + 1.4f * b, top + (float)shoulders_l * row + 0.6f * b + (shoulders_l ? 0.25f * b : 0), 2.6f * b, 1.2f * b, false, s);
        tc_place(tc_find_button(t, BR_PAD_START), W - m - 1.4f * b, top + (float)shoulders_r * row + 0.6f * b + (shoulders_r ? 0.25f * b : 0), 2.6f * b, 1.2f * b, false, s);
        tc_place(tc_find_button(t, BR_PAD_GUIDE), W - m - 1.4f * b, top + (float)shoulders_r * row + 2.2f * b, 2.6f * b, 1.2f * b, false, s);
    }
    tc_build_items(t);
}

/* ---- touching them ------------------------------------------------------------------- */

/* The control a finger at (x, y) works: the nearest whose area it is in. `buttons_only`: not the stick. */
static int tc_hit(const br_touch_ui *t, float x, float y, bool buttons_only, float slack) {
    int i, best = -1;
    float best_score = 1;
    for (i = 0; i < t->count; ++i) {
        const br_touch_control *c = &t->controls[i];
        float dx = x - c->x, dy = y - c->y, score;
        if (c->w <= 0) continue;
        if (c->button == BR_TOUCH_STICK) {
            if (buttons_only) continue;
            score = sqrtf(dx * dx + dy * dy) / (c->w * 0.75f) + 0.15f;   /* (a button near the stick wins) */
        } else if (c->round) {
            float r = c->w * 0.5f;
            score = sqrtf(dx * dx + dy * dy) / ((r * 1.25f > r + 8 * t->scale ? r * 1.25f : r + 8 * t->scale) * slack);
        } else {
            float hx = c->w * 0.5f + c->h * 0.3f, hy = c->h * 0.5f + c->h * 0.3f;
            score = fabsf(dx) / (hx * slack) > fabsf(dy) / (hy * slack) ? fabsf(dx) / (hx * slack) : fabsf(dy) / (hy * slack);
        }
        if (score < best_score) { best_score = score; best = i; }
    }
    return best;
}

static void tc_stick_to(br_touch_control *c, float x, float y) {
    float r = c->w * 0.5f * 0.8f, sx = (x - c->x) / r, sy = (y - c->y) / r, len = sqrtf(sx * sx + sy * sy);
    if (len > 1) { sx /= len; sy /= len; }
    c->sx = sx;
    c->sy = sy;
}

static void tc_press(br_touch_control *c, int finger, float x, float y) {
    c->finger = finger;
    c->down = true;
    if (c->button == BR_TOUCH_STICK) tc_stick_to(c, x, y);
}

static void tc_let_go(br_touch_control *c) {
    c->finger = -1;
    c->down = false;
    c->sx = c->sy = 0;
}

static void tc_release_all(br_touch_ui *t) {
    int i;
    for (i = 0; i < t->count; ++i) tc_let_go(&t->controls[i]);
    for (i = 0; i < t->finger_count; ++i) if (t->fingers[i].control >= 0) t->fingers[i].control = FINGER_NONE;
}

static int tc_finger_at(const br_touch_ui *t, int id) {
    int i;
    for (i = 0; i < t->finger_count; ++i) if (t->fingers[i].id == id) return i;
    return -1;
}

static bool tc_mouse_taken(const br_touch_ui *t) {
    int i;
    for (i = 0; i < t->finger_count; ++i) if (t->fingers[i].control == FINGER_MOUSE) return true;
    return false;
}

typedef struct tc_out_events { br_event list[64]; int count; } tc_out_events;
static void tc_out(tc_out_events *o, br_event e) { if (o->count < (int)BR_ARRAY_COUNT(o->list)) o->list[o->count++] = e; }

static void tc_mouse_event(tc_out_events *o, uint32_t type, float x, float y) {
    br_event e;
    if (type == BR_EVENT_MOUSE_MOVE) { e = br_event_mouse_move(x, y, 0, 0); e.u.mouse_move.touch = true; }
    else { e = br_event_mouse_button(type, BR_MOUSE_LEFT, x, y); e.u.mouse_button.touch = true; }
    tc_out(o, e);
}

/* What the controls hold, posted as the virtual gamepad's (and the keys') events. */
static void tc_post_state(br_touch_ui *t, br_events *events, br_input *in) {
    uint8_t want[BR_KEY_COUNT];
    br_gamepad_raw raw;
    bool uses_pad = false;
    int i, k;
    memset(want, 0, sizeof(want));
    memset(&raw, 0, sizeof(raw));
    for (i = 0; i < t->count; ++i) {
        const br_touch_control *c = &t->controls[i];
        if (c->keys[0]) {
            if (c->button == BR_TOUCH_STICK) {
                if (c->down && c->sy < -0.45f) want[c->keys[0]]++;
                if (c->down && c->sy > 0.45f) want[c->keys[1]]++;
                if (c->down && c->sx < -0.45f) want[c->keys[2]]++;
                if (c->down && c->sx > 0.45f) want[c->keys[3]]++;
            } else if (c->down) {
                want[c->keys[0]]++;
            }
            continue;
        }
        uses_pad = true;
        if (!c->down) continue;
        if (c->button == BR_TOUCH_STICK) { raw.axes[BR_PAD_AXIS_LEFT_X] = c->sx; raw.axes[BR_PAD_AXIS_LEFT_Y] = c->sy; }
        else if (c->button == TOUCH_LT) raw.axes[BR_PAD_AXIS_LEFT_TRIGGER] = 1;
        else if (c->button == TOUCH_RT) raw.axes[BR_PAD_AXIS_RIGHT_TRIGGER] = 1;
        else raw.buttons |= 1u << c->button;
    }
    for (k = 1; k < BR_KEY_COUNT; ++k) {
        if ((want[k] > 0) == (t->keys_held[k] > 0)) continue;
        {
            br_event e = br_event_make(want[k] ? BR_EVENT_KEY_DOWN : BR_EVENT_KEY_UP);
            e.u.key.key = (br_key)k;
            br_events_post(events, &e);
        }
    }
    memcpy(t->keys_held, want, sizeof(want));
    if (uses_pad && t->pad < 0 && br_touch_ui_visible(t)) {      /* the first time: a free gamepad slot (the last free one) */
        for (i = BR_MAX_GAMEPADS - 1; i >= 0 && in->pads[i].connected; --i) {}
        t->pad = i;
    }
    if (t->pad >= 0) {
        raw.connected = true;
        snprintf(raw.name, sizeof(raw.name), "Touch controls");
        in->touch_pad = t->pad + 1;
        br_gamepads_sync(&t->tracker, t->pad, &raw, events);
    }
}

void br_touch_ui_process(br_touch_ui *t, br_events *events, br_input *in) {
    tc_out_events o;
    size_t i, n = br_events_pending(events);
    o.count = 0;
    for (i = 0; i < n; ++i) {
        br_event *ev = br_events_peek(events, i);
        if (t->mouse_as_touch) {                                 /* testing on a desktop: the mouse is a finger */
            if (ev->type == BR_EVENT_MOUSE_BUTTON_DOWN && ev->u.mouse_button.button == BR_MOUSE_LEFT && !ev->u.mouse_button.touch) {
                *ev = br_event_touch(BR_EVENT_TOUCH_DOWN, MOUSE_FINGER, ev->u.mouse_button.x, ev->u.mouse_button.y);
                t->mouse_down = true;
            } else if (ev->type == BR_EVENT_MOUSE_BUTTON_UP && ev->u.mouse_button.button == BR_MOUSE_LEFT && !ev->u.mouse_button.touch && t->mouse_down) {
                *ev = br_event_touch(BR_EVENT_TOUCH_UP, MOUSE_FINGER, ev->u.mouse_button.x, ev->u.mouse_button.y);
                t->mouse_down = false;
            } else if (ev->type == BR_EVENT_MOUSE_MOVE && t->mouse_down && !ev->u.mouse_move.touch) {
                *ev = br_event_touch(BR_EVENT_TOUCH_MOVE, MOUSE_FINGER, ev->u.mouse_move.x, ev->u.mouse_move.y);
            }
        }
        switch (ev->type) {
        case BR_EVENT_TOUCH_DOWN: {
            float x = ev->u.touch.wx, y = ev->u.touch.wy;
            int c;
            if (t->mode == BR_TOUCH_AUTO && t->count > 0 && !t->shown) {
                t->shown = true;                                 /* the first touch shows them (and works the mouse) */
                br_touch_ui_layout(t, t->win_w, t->win_h, t->scale, t->viewport);
                c = -1;
            } else {
                c = br_touch_ui_visible(t) ? tc_hit(t, x, y, false, 1) : -1;
            }
            if (tc_finger_at(t, ev->u.touch.id) >= 0 || t->finger_count == BR_MAX_TOUCHES) break;
            t->fingers[t->finger_count].id = ev->u.touch.id;
            if (c >= 0 && t->controls[c].finger < 0) {
                t->fingers[t->finger_count].control = c;
                tc_press(&t->controls[c], ev->u.touch.id, x, y);
                ev->u.touch.claimed = true;
            } else if (c < 0 && !tc_mouse_taken(t)) {
                t->fingers[t->finger_count].control = FINGER_MOUSE;
                tc_mouse_event(&o, BR_EVENT_MOUSE_MOVE, x, y);
                tc_mouse_event(&o, BR_EVENT_MOUSE_BUTTON_DOWN, x, y);
            } else {
                t->fingers[t->finger_count].control = FINGER_NONE;
                ev->u.touch.claimed = c >= 0;
            }
            t->finger_count++;
            break;
        }
        case BR_EVENT_TOUCH_MOVE: {
            int f = tc_finger_at(t, ev->u.touch.id), c;
            float x = ev->u.touch.wx, y = ev->u.touch.wy;
            if (f < 0) break;
            c = t->fingers[f].control;
            if (c == FINGER_MOUSE) { tc_mouse_event(&o, BR_EVENT_MOUSE_MOVE, x, y); break; }
            if (c < 0) break;
            ev->u.touch.claimed = true;
            if (t->controls[c].button == BR_TOUCH_STICK) { tc_stick_to(&t->controls[c], x, y); break; }
            {                                                    /* sliding from one button onto another */
                int to = tc_hit(t, x, y, true, 1);
                if (to == c || (to < 0 && tc_hit(t, x, y, true, 1.6f) == c)) break;
                tc_let_go(&t->controls[c]);
                if (to >= 0 && t->controls[to].finger < 0) { tc_press(&t->controls[to], ev->u.touch.id, x, y); t->fingers[f].control = to; }
                else t->fingers[f].control = FINGER_NONE;
            }
            break;
        }
        case BR_EVENT_TOUCH_UP: {
            int f = tc_finger_at(t, ev->u.touch.id), c;
            if (f < 0) break;
            c = t->fingers[f].control;
            if (c == FINGER_MOUSE) tc_mouse_event(&o, BR_EVENT_MOUSE_BUTTON_UP, ev->u.touch.wx, ev->u.touch.wy);
            else if (c >= 0) { tc_let_go(&t->controls[c]); ev->u.touch.claimed = true; }
            t->fingers[f] = t->fingers[--t->finger_count];
            break;
        }
        case BR_EVENT_KEY_DOWN:                                  /* back to the keys or a gamepad: hide them */
            if (t->mode == BR_TOUCH_AUTO && t->shown && !ev->u.key.repeat) { t->shown = false; tc_release_all(t); }
            break;
        case BR_EVENT_GAMEPAD_BUTTON_DOWN:
            if (t->mode == BR_TOUCH_AUTO && t->shown && ev->u.gamepad.pad != t->pad) { t->shown = false; tc_release_all(t); }
            break;
        default: break;
        }
    }
    for (i = 0; i < (size_t)o.count; ++i) br_events_post(events, &o.list[i]);
    tc_post_state(t, events, in);
    tc_build_items(t);
}

/* ---- drawing ---------------------------------------------------------------------------- */

static uint32_t tc_with_alpha(uint32_t rgb, uint32_t a) { return (rgb & 0xffffff00u) | (a & 0xffu); }

static uint32_t tc_button_colour(int b) {
    switch (b) {
    case BR_PAD_A: return 0x5cc85c00u;
    case BR_PAD_B: return 0xe8555a00u;
    case BR_PAD_X: return 0x4f8fe800u;
    case BR_PAD_Y: return 0xf0c04000u;
    default: return 0xd8d8d800u;
    }
}

static br_overlay_item *tc_item(br_touch_ui *t, int kind, float x, float y, float w, float h) {
    br_overlay_item *it;
    if (t->item_count >= (int)BR_ARRAY_COUNT(t->items)) return NULL;
    it = &t->items[t->item_count++];
    memset(it, 0, sizeof(*it));
    it->kind = kind;
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->line_width = 2 * t->scale;
    return it;
}

static void tc_build_items(br_touch_ui *t) {
    int i;
    t->item_count = 0;
    if (!br_touch_ui_visible(t)) return;
    for (i = 0; i < t->count; ++i) {
        const br_touch_control *c = &t->controls[i];
        br_overlay_item *it;
        if (c->w <= 0) continue;
        if (c->button == BR_TOUCH_STICK) {                       /* a ring, and the knob where the finger pushes it */
            float r = c->w * 0.5f;
            if ((it = tc_item(t, BR_OVERLAY_CIRCLE, c->x, c->y, c->w, c->w)) != NULL) { it->fill = 0xffffff1cu; it->line = 0xffffff66u; }
            if ((it = tc_item(t, BR_OVERLAY_CIRCLE, c->x + c->sx * r * 0.55f, c->y + c->sy * r * 0.55f, c->w * 0.46f, c->w * 0.46f)) != NULL) {
                it->fill = c->down ? 0xffffff99u : 0xffffff55u;
                it->line = 0xffffffb0u;
            }
            continue;
        }
        {
            uint32_t col = tc_button_colour(c->button);
            if ((it = tc_item(t, c->round ? BR_OVERLAY_CIRCLE : BR_OVERLAY_PILL, c->x, c->y, c->w, c->h)) != NULL) {
                it->fill = tc_with_alpha(col, c->down ? 0xb0 : 0x48);
                it->line = tc_with_alpha(col, 0xd0);
            }
            if (c->label[0] && (it = tc_item(t, BR_OVERLAY_TEXT, c->x, c->y, c->w, (c->round ? c->w * 0.42f : c->h * 0.56f) * (strlen(c->label) > 2 && c->round ? 0.7f : 1))) != NULL) {
                it->fill = c->down ? 0xffffffffu : 0xffffffe0u;
                snprintf(it->text, sizeof(it->text), "%s", c->label);
            }
        }
    }
}

int br_touch_ui_overlay(const br_touch_ui *t, const br_overlay_item **items) {
    if (items) *items = t->items;
    return br_touch_ui_visible(t) ? t->item_count : 0;
}

void br_touch_ui_draw(const br_touch_ui *t, br_renderer *r, const br_viewport *vp) {
    int i;
    if (!br_touch_ui_visible(t) || vp->scale_x <= 0 || vp->scale_y <= 0) return;
    for (i = 0; i < t->item_count; ++i) {
        const br_overlay_item *it = &t->items[i];
        br_vec2 c = br_viewport_to_framebuffer(vp, br_v2(it->x, it->y));
        float w = it->w / vp->scale_x, h = it->h / vp->scale_y;
        switch (it->kind) {
        case BR_OVERLAY_CIRCLE:
            if (it->fill & 0xff) br_fill_circle(r, c, w * 0.5f, br_hex(it->fill));
            if (it->line & 0xff) br_draw_circle(r, c, w * 0.5f, br_hex(it->line));
            break;
        case BR_OVERLAY_PILL: {
            br_rect rect;
            rect.x = c.x - w * 0.5f;
            rect.y = c.y - h * 0.5f;
            rect.w = w;
            rect.h = h;
            if (it->fill & 0xff) br_fill_rect(r, rect, br_hex(it->fill));
            if (it->line & 0xff) br_draw_rect(r, rect, br_hex(it->line));
            break;
        }
        case BR_OVERLAY_TEXT:
            br_draw_text(r, it->text, c.x - (float)strlen(it->text) * 4 + 0.5f, c.y - 3.5f, 1, br_hex(it->fill));
            break;
        default: break;
        }
    }
}
