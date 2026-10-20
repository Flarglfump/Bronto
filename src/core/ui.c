/*
 * Bronto Engine - immediate-mode GUI (see ui.h).
 *
 * The usual immediate-mode scheme: each widget hashes its label into an
 * id, tests the mouse against its rectangle, and the one widget the mouse
 * is holding is "active". Only the state that must outlive a frame is
 * kept: the active widget, the text being edited, open drop-downs and
 * panel scroll positions.
 */
#include "bronto/ui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { EDIT_TEXT = 1, EDIT_FLOAT, EDIT_INT, EDIT_U8 };
enum { B_HOVER = BR_UI_HOVERED, B_PRESS = BR_UI_PRESSED, B_HELD = BR_UI_HELD, B_CLICK = BR_UI_CLICKED,
       B_RELEASE = BR_UI_RELEASED, B_DOUBLE = BR_UI_DOUBLE_CLICKED };
#define DRAG_ACTIVE_ID 1u        /* ui->active while a drag is carried */
#define DRAG_THRESHOLD 4.0f
#define COLUMN_GAP 4.0f
#define SCROLLBAR_W 5.0f

br_ui_style br_ui_default_style(void) {
    br_ui_style s;
    s.background    = br_rgb(30, 34, 40);
    s.panel         = br_rgb(38, 43, 51);
    s.header        = br_rgb(48, 55, 68);
    s.button        = br_rgb(58, 66, 80);
    s.button_hot    = br_rgb(74, 84, 102);
    s.button_active = br_rgb(90, 107, 133);
    s.field         = br_rgb(27, 31, 37);
    s.text          = br_rgb(230, 233, 239);
    s.text_dim      = br_rgb(154, 163, 178);
    s.accent        = br_rgb(90, 160, 255);
    s.border        = br_rgb(20, 23, 28);
    s.selection     = br_rgb(61, 95, 143);
    s.popup         = br_rgb(45, 51, 61);
    s.font = 1.0f;
    s.row = 16.0f;
    s.spacing = 3.0f;
    s.padding = 6.0f;
    s.label_frac = 0.38f;
    return s;
}

void br_ui_init(br_ui *ui) {
    memset(ui, 0, sizeof(*ui));
    ui->style = br_ui_default_style();
}

/* ---- ids ----------------------------------------------------------------- */

static br_ui_id hash_str(br_ui_id seed, const char *s) {
    br_ui_id h = seed;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h ? h : 1;
}

static br_ui_id id_of(const br_ui *ui, const char *label) {
    return hash_str(ui->id_depth ? ui->ids[ui->id_depth - 1] : 2166136261u, label);
}

void br_ui_push_id(br_ui *ui, const char *key) {
    br_ui_id id = id_of(ui, key);
    if (ui->id_depth < BR_UI_ID_STACK) ui->ids[ui->id_depth++] = id;
}

void br_ui_push_id_int(br_ui *ui, int key) {
    char buf[16];
    snprintf(buf, sizeof(buf), "#%d", key);
    br_ui_push_id(ui, buf);
}

void br_ui_pop_id(br_ui *ui) { if (ui->id_depth > 0) ui->id_depth--; }

/* Labels show the text before "##". */
static size_t visible_len(const char *label) {
    const char *hash = strstr(label, "##");
    return hash ? (size_t)(hash - label) : strlen(label);
}

/* ---- geometry and clipping ------------------------------------------------ */

static br_ui_panel *top(br_ui *ui) { return ui->panel_depth ? &ui->panels[ui->panel_depth - 1] : NULL; }

static br_recti to_recti(br_rect r) {
    int x0 = (int)floorf(r.x), y0 = (int)floorf(r.y), x1 = (int)ceilf(r.x + r.w), y1 = (int)ceilf(r.y + r.h);
    return br_recti_make(x0, y0, x1 - x0, y1 - y0);
}

static br_recti intersect(br_recti a, br_recti b) {
    int x0 = BR_MAX(a.x, b.x), y0 = BR_MAX(a.y, b.y);
    int x1 = BR_MIN(a.x + a.w, b.x + b.w), y1 = BR_MIN(a.y + a.h, b.y + b.h);
    return br_recti_make(x0, y0, BR_MAX(0, x1 - x0), BR_MAX(0, y1 - y0));
}

static bool in_rect(br_rect r, br_vec2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h; }

static bool in_clip(br_recti c, br_vec2 p) {
    return p.x >= (float)c.x && p.y >= (float)c.y && p.x < (float)(c.x + c.w) && p.y < (float)(c.y + c.h);
}

static br_recti current_clip(br_ui *ui) { br_ui_panel *p = top(ui); return p ? p->clip : ui->screen_clip; }
static void restore_clip(br_ui *ui) { br_set_clip(ui->r, current_clip(ui)); }

static bool popup_covers(const br_ui *ui, br_vec2 p) { return ui->popup_id && in_rect(ui->popup_rect, p); }

static bool hovering(br_ui *ui, br_rect r) {
    if (ui->disabled || !in_rect(r, ui->mouse) || popup_covers(ui, ui->mouse)) return false;
    return in_clip(current_clip(ui), ui->mouse);
}

/* The mouse behaviour every clickable widget shares. */
static int behave(br_ui *ui, br_ui_id id, br_rect r) {
    int f = 0;
    bool over = hovering(ui, r);
    if (over && (ui->active == 0 || ui->active == id)) f |= B_HOVER;
    if ((f & B_HOVER) && ui->pressed && ui->active == 0) {
        ui->active = id;
        ui->press_mouse = ui->mouse;
        ui->dragging = false;
        f |= B_PRESS;
    }
    if (ui->active == id) {
        ui->active_seen = true;
        if (ui->down) f |= B_HELD;
        if (ui->released) {
            f |= B_RELEASE;
            if (over) f |= B_CLICK;
            ui->active = 0;
        }
    }
    if (f & B_CLICK) {
        if (ui->click_id == id && ui->time - ui->click_time <= BR_UI_DOUBLE_CLICK_TIME) {
            f |= B_DOUBLE;
            ui->click_id = 0;
        } else {
            ui->click_id = id;
            ui->click_time = ui->time;
        }
    }
    ui->last_id = id;
    ui->last_rect = r;
    ui->last_flags = f;
    return f;
}

/* ---- drawing helpers ---------------------------------------------------------- */

static br_rect inset(br_rect r, float d) { return br_rect_make(r.x + d, r.y + d, r.w - 2 * d, r.h - 2 * d); }

/* `len` characters of `text` inside `box`, clipped to it; align 0 = left, 0.5 = centred. */
static void text_in(br_ui *ui, const char *text, size_t len, br_rect box, float align, float pad, br_color c) {
    char tmp[BR_UI_TEXT_MAX];
    br_vec2 size;
    float x, y;
    if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
    memcpy(tmp, text, len);
    tmp[len] = 0;
    size = br_measure_text(tmp, ui->style.font);
    x = box.x + pad + (box.w - 2 * pad - size.x) * align;
    if (x < box.x + pad) x = box.x + pad;
    y = box.y + floorf((box.h - 8.0f * ui->style.font) / 2.0f + 0.5f);
    br_set_clip(ui->r, intersect(current_clip(ui), to_recti(box)));
    br_draw_text(ui->r, tmp, floorf(x), y, ui->style.font, c);
    restore_clip(ui);
}

static void box_fill(br_ui *ui, br_rect r, br_color fill, br_color border) {
    br_fill_rect(ui->r, r, fill);
    br_draw_rect(ui->r, r, border);
}

static br_color button_color(const br_ui *ui, br_ui_id id, int f) {
    if (ui->active == id && (f & B_HELD)) return ui->style.button_active;
    return (f & B_HOVER) ? ui->style.button_hot : ui->style.button;
}

/* A field row: the label on the left, the value's rectangle returned. */
static br_rect field_value(br_ui *ui, const char *label, br_rect row) {
    size_t n = visible_len(label);
    float lw;
    if (n == 0) return row;
    lw = floorf(row.w * ui->style.label_frac);
    text_in(ui, label, n, br_rect_make(row.x, row.y, lw - 4, row.h), 0, 0, ui->style.text_dim);
    return br_rect_make(row.x + lw, row.y, row.w - lw, row.h);
}

/* ---- frame ---------------------------------------------------------------------- */

static double read_num(const void *p, int kind) {
    if (kind == EDIT_FLOAT) return *(const float *)p;
    if (kind == EDIT_INT) return *(const int *)p;
    return *(const uint8_t *)p;
}

static void write_num(void *p, int kind, double v, double min, double max) {
    if (min < max) v = v < min ? min : v > max ? max : v;
    if (kind == EDIT_FLOAT) *(float *)p = (float)v;
    else if (kind == EDIT_INT) *(int *)p = (int)floor(v + 0.5);
    else *(uint8_t *)p = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : floor(v + 0.5));
}

static bool commit_edit(br_ui *ui) {
    bool ok = false;
    if (!ui->edit_id) return false;
    if (ui->edit_kind == EDIT_TEXT) {
        size_t n = (size_t)ui->edit_len < ui->edit_size ? (size_t)ui->edit_len : ui->edit_size - 1;
        memcpy(ui->edit_ptr, ui->edit, n);
        ((char *)ui->edit_ptr)[n] = 0;
        ok = true;
    } else {
        char *end;
        double v = strtod(ui->edit, &end);
        if (end != ui->edit && isfinite(v)) {
            write_num(ui->edit_ptr, ui->edit_kind, v, ui->edit_min, ui->edit_max);
            ok = true;
        }
    }
    ui->edit_id = 0;
    if (ok) ui->edits_committed++;
    return ok;
}

void br_ui_begin(br_ui *ui, br_renderer *r, const br_input *in, double time) {
    ui->r = r;
    ui->in = in;
    ui->time = time;
    ui->mouse = br_v2(in->mouse_x, in->mouse_y);
    ui->down = br_mouse_down(in, BR_MOUSE_LEFT);
    ui->pressed = br_mouse_pressed(in, BR_MOUSE_LEFT);
    ui->released = br_mouse_released(in, BR_MOUSE_LEFT);
    ui->wheel = in->wheel_y;
    ui->wheel_used = false;
    ui->screen_clip = r->clip;
    ui->panel_depth = 0;
    ui->id_depth = 0;
    ui->area_count = 0;
    ui->active_seen = false;
    ui->edit_seen = false;
    ui->last_id = 0;
    ui->last_rect = br_rect_make(0, 0, 0, 0);
    ui->last_flags = 0;
    if (ui->drag.active && br_key_pressed(in, BR_KEY_ESCAPE)) br_ui_drag_cancel(ui);
    /* Clicking anywhere else commits the field being edited. */
    if (ui->edit_id && ui->pressed && !in_rect(ui->edit_rect, ui->mouse)) commit_edit(ui);
}

static void draw_popup(br_ui *ui) {
    int i;
    float h = ui->style.row;
    br_set_clip(ui->r, ui->screen_clip);
    box_fill(ui, ui->popup_rect, ui->style.popup, ui->style.accent);
    for (i = 0; i < ui->popup_count; ++i) {
        br_rect item = br_rect_make(ui->popup_rect.x + 1, ui->popup_rect.y + 1 + (float)i * h, ui->popup_rect.w - 2, h);
        bool over = !ui->disabled && in_rect(item, ui->mouse);
        if (over || i == ui->popup_selected) br_fill_rect(ui->r, item, over ? ui->style.button_hot : ui->style.selection);
        text_in(ui, ui->popup_items[i], strlen(ui->popup_items[i]), item, 0, 4, ui->style.text);
        if (over && ui->pressed && !ui->popup_just_opened) {
            ui->popup_result_id = ui->popup_id;
            ui->popup_result = i;
        }
    }
    if (ui->pressed && !ui->popup_just_opened) ui->popup_id = 0;   /* any click closes it */
    ui->popup_just_opened = false;
}

static void draw_drag(br_ui *ui) {
    const char *text = ui->drag.label[0] ? ui->drag.label : ui->drag.payload;
    br_vec2 size = br_measure_text(text, ui->style.font);
    br_rect box = br_rect_make(floorf(ui->mouse.x + 10), floorf(ui->mouse.y + 6), size.x + 8, ui->style.row);
    br_set_clip(ui->r, ui->screen_clip);
    br_fill_rect(ui->r, box, br_rgba(ui->style.popup.r, ui->style.popup.g, ui->style.popup.b, 230));
    br_draw_rect(ui->r, box, ui->style.accent);
    text_in(ui, text, strlen(text), box, 0, 4, ui->style.text);
}

void br_ui_end(br_ui *ui) {
    if (ui->popup_id) draw_popup(ui);
    if (ui->drag.active) {
        if (ui->released || !ui->down) {
            ui->drag.active = false;                 /* dropped (targets saw it this frame) */
            ui->active = 0;
        } else {
            ui->active_seen = true;                  /* the drag keeps the mouse */
            draw_drag(ui);
        }
    }
    if (!ui->down || !ui->active_seen) ui->active = 0;
    if (ui->edit_id && !ui->edit_seen) ui->edit_id = 0;      /* its field went away */
    memcpy(ui->areas_prev, ui->areas, sizeof(ui->areas));
    ui->area_count_prev = ui->area_count;
    br_set_clip(ui->r, ui->screen_clip);
}

bool br_ui_wants_mouse(const br_ui *ui, br_vec2 p) {
    int i;
    if (ui->active || popup_covers(ui, p)) return true;
    for (i = 0; i < ui->area_count_prev; ++i) if (in_rect(ui->areas_prev[i], p)) return true;
    for (i = 0; i < ui->area_count; ++i) if (in_rect(ui->areas[i], p)) return true;
    return false;
}

bool br_ui_wants_keyboard(const br_ui *ui) { return ui->edit_id != 0; }
void br_ui_set_disabled(br_ui *ui, bool disabled) { ui->disabled = disabled; }

/* ---- layout --------------------------------------------------------------------- */

static int scroll_slot(br_ui *ui, br_ui_id id) {
    int i;
    for (i = 0; i < ui->scroll_count; ++i) if (ui->scrolls[i].id == id) return i;
    if (ui->scroll_count < BR_UI_SCROLL_SLOTS) {
        i = ui->scroll_count++;
        ui->scrolls[i].id = id;
        ui->scrolls[i].scroll = 0;
        ui->scrolls[i].content = 0;
        return i;
    }
    return -1;
}

static float avail_right(const br_ui *ui, const br_ui_panel *p) {
    float right = p->rect.x + p->rect.w - ui->style.padding;
    if (p->scroll_slot >= 0 && ui->scrolls[p->scroll_slot].content > p->rect.h) right -= SCROLLBAR_W;
    return right;
}

void br_ui_panel_begin(br_ui *ui, const char *name, br_rect rect) {
    br_recti parent = current_clip(ui);
    br_ui_panel *p;
    if (ui->panel_depth >= BR_UI_PANEL_STACK) return;
    p = &ui->panels[ui->panel_depth++];
    memset(p, 0, sizeof(*p));
    p->id = id_of(ui, name);
    p->rect = rect;
    p->clip = intersect(parent, to_recti(rect));
    p->scroll_slot = scroll_slot(ui, p->id);
    p->scroll = p->scroll_slot >= 0 ? ui->scrolls[p->scroll_slot].scroll : 0;
    p->x = rect.x + ui->style.padding;
    p->y = rect.y + ui->style.padding - p->scroll;
    p->bottom = p->y;
    if (ui->area_count < BR_UI_MAX_PANELS) ui->areas[ui->area_count++] = rect;
    br_set_clip(ui->r, p->clip);
    br_fill_rect(ui->r, rect, ui->style.panel);
    if (ui->id_depth < BR_UI_ID_STACK) ui->ids[ui->id_depth++] = p->id;
}

void br_ui_panel_end(br_ui *ui) {
    br_ui_panel *p = top(ui);
    float content, max_scroll;
    if (!p) return;
    content = p->bottom - (p->rect.y - p->scroll) + ui->style.padding;
    max_scroll = content > p->rect.h ? content - p->rect.h : 0;
    if (ui->wheel != 0 && !ui->wheel_used && !ui->disabled && in_rect(p->rect, ui->mouse) && !popup_covers(ui, ui->mouse)) {
        p->scroll -= ui->wheel * ui->style.row * 3;
        ui->wheel_used = true;
    }
    p->scroll = p->scroll < 0 ? 0 : p->scroll > max_scroll ? max_scroll : p->scroll;
    if (p->scroll_slot >= 0) {
        ui->scrolls[p->scroll_slot].scroll = p->scroll;
        ui->scrolls[p->scroll_slot].content = content;
    }
    if (max_scroll > 0) {
        float th = BR_MAX(12.0f, p->rect.h * p->rect.h / content);
        float ty = p->rect.y + (p->rect.h - th) * (p->scroll / max_scroll);
        br_fill_rect(ui->r, br_rect_make(p->rect.x + p->rect.w - SCROLLBAR_W, ty, SCROLLBAR_W - 1, th), ui->style.button_hot);
    }
    ui->panel_depth--;
    br_ui_pop_id(ui);
    restore_clip(ui);
}

br_rect br_ui_row(br_ui *ui, float height) {
    br_ui_panel *p = top(ui);
    br_rect r;
    if (height <= 0) height = ui->style.row;
    if (!p) return br_rect_make(0, 0, 0, height);
    if (p->placed) {
        p->placed = false;
        return p->place;
    }
    if (p->cols_left > 0) {
        r = br_rect_make(p->col_x, p->row_y, p->col_w, ui->style.row);
        p->col_x += p->col_w + COLUMN_GAP;
        if (--p->cols_left == 0) p->y = p->row_y + ui->style.row + ui->style.spacing;
    } else {
        float x = p->x + p->indent, w = avail_right(ui, p) - x;
        r = br_rect_make(x, p->y, w > 1 ? w : 1, height);
        p->y += height + ui->style.spacing;
    }
    if (r.y + r.h > p->bottom) p->bottom = r.y + r.h;
    return r;
}

void br_ui_columns(br_ui *ui, int count) {
    br_ui_panel *p = top(ui);
    float x, w;
    if (!p || count < 1) return;
    x = p->x + p->indent;
    w = avail_right(ui, p) - x;
    p->cols_left = count;
    p->col_w = floorf((w - COLUMN_GAP * (float)(count - 1)) / (float)count);
    p->col_x = x;
    p->row_y = p->y;
}

void br_ui_place(br_ui *ui, br_rect rect) {
    br_ui_panel *p = top(ui);
    if (!p) return;
    p->placed = true;
    p->place = rect;
}

void br_ui_spacing(br_ui *ui, float pixels) { br_ui_panel *p = top(ui); if (p) p->y += pixels; }
void br_ui_indent(br_ui *ui, float pixels) { br_ui_panel *p = top(ui); if (p) p->indent += pixels; }

/* ---- simple widgets ----------------------------------------------------------------- */

void br_ui_label_colored(br_ui *ui, const char *text, br_color color) {
    br_rect r = br_ui_row(ui, 0);
    text_in(ui, text, strlen(text), r, 0, 0, color);
}

void br_ui_label(br_ui *ui, const char *text) { br_ui_label_colored(ui, text, ui->style.text); }

void br_ui_labelf(br_ui *ui, const char *fmt, ...) {
    char buf[BR_UI_TEXT_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    br_ui_label(ui, buf);
}

void br_ui_heading(br_ui *ui, const char *text) {
    br_rect r = br_ui_row(ui, 0);
    text_in(ui, text, strlen(text), r, 0, 0, ui->style.accent);
    br_fill_rect(ui->r, br_rect_make(r.x, r.y + r.h - 1, r.w, 1), ui->style.header);
}

void br_ui_separator(br_ui *ui) {
    br_rect r = br_ui_row(ui, 5);
    br_fill_rect(ui->r, br_rect_make(r.x, r.y + 2, r.w, 1), ui->style.header);
}

static bool button_impl(br_ui *ui, const char *label, bool lit) {
    br_ui_id id = id_of(ui, label);
    br_rect r = br_ui_row(ui, 0);
    int f = behave(ui, id, r);
    br_color bg = button_color(ui, id, f);
    if (lit) bg = (f & B_HOVER) ? br_color_lerp(ui->style.selection, BR_WHITE, 0.1f) : ui->style.selection;
    box_fill(ui, r, bg, lit ? ui->style.accent : ui->style.border);
    text_in(ui, label, visible_len(label), r, 0.5f, 3, ui->disabled ? ui->style.text_dim : ui->style.text);
    return (f & B_CLICK) != 0;
}

bool br_ui_button(br_ui *ui, const char *label) { return button_impl(ui, label, false); }
bool br_ui_toggle(br_ui *ui, const char *label, bool on) { return button_impl(ui, label, on); }

bool br_ui_checkbox(br_ui *ui, const char *label, bool *value) {
    br_ui_id id = id_of(ui, label);
    br_rect v = field_value(ui, label, br_ui_row(ui, 0));
    br_rect box = br_rect_make(v.x, v.y + (v.h - 12) / 2, 12, 12);
    int f = behave(ui, id, v);
    box_fill(ui, box, (f & B_HOVER) ? ui->style.button_hot : ui->style.field, ui->style.border);
    if (*value) br_fill_rect(ui->r, inset(box, 3), ui->style.accent);
    if (f & B_CLICK) { *value = !*value; return true; }
    return false;
}

bool br_ui_selectable(br_ui *ui, const char *label, bool selected) {
    br_ui_id id = id_of(ui, label);
    br_rect r = br_ui_row(ui, 0);
    int f = behave(ui, id, r);
    if (selected) br_fill_rect(ui->r, r, ui->style.selection);
    else if (f & B_HOVER) br_fill_rect(ui->r, r, ui->style.header);
    text_in(ui, label, visible_len(label), r, 0, 4, selected ? BR_WHITE : ui->style.text);
    return (f & B_CLICK) != 0;
}

bool br_ui_header(br_ui *ui, const char *label, bool *open, bool *remove) {
    br_ui_id id = id_of(ui, label);
    br_rect r = br_ui_row(ui, ui->style.row + 2);
    br_rect x = br_rect_make(r.x + r.w - r.h, r.y, r.h, r.h);
    float cy = r.y + r.h / 2, cx = r.x + 8;
    int f, fx = 0;
    if (remove) fx = behave(ui, hash_str(id, "remove"), x);
    f = (fx & B_HOVER) ? 0 : behave(ui, id, r);
    br_fill_rect(ui->r, r, (f & B_HOVER) ? ui->style.button : ui->style.header);
    if (*open) br_fill_triangle(ui->r, br_v2(cx - 4, cy - 2), br_v2(cx + 4, cy - 2), br_v2(cx, cy + 3), ui->style.text_dim);
    else br_fill_triangle(ui->r, br_v2(cx - 2, cy - 4), br_v2(cx + 3, cy), br_v2(cx - 2, cy + 4), ui->style.text_dim);
    text_in(ui, label, visible_len(label), br_rect_make(r.x + 16, r.y, r.w - 16 - (remove ? r.h : 0), r.h), 0, 0, ui->style.text);
    if (remove) {
        br_color xc = (fx & B_HOVER) ? br_rgb(255, 120, 110) : ui->style.text_dim;
        br_draw_line(ui->r, br_v2(x.x + 5, x.y + 5), br_v2(x.x + x.w - 5, x.y + x.h - 5), xc);
        br_draw_line(ui->r, br_v2(x.x + x.w - 5, x.y + 5), br_v2(x.x + 5, x.y + x.h - 5), xc);
        if (fx & B_CLICK) *remove = true;
    }
    if (f & B_CLICK) *open = !*open;
    return *open;
}

/* ---- text editing ----------------------------------------------------------------- */

static void begin_edit(br_ui *ui, br_ui_id id, br_rect box, void *ptr, int kind, size_t size,
                       double min, double max, const char *initial) {
    ui->edit_id = id;
    ui->edit_ptr = ptr;
    ui->edit_kind = kind;
    ui->edit_size = size;
    ui->edit_min = min;
    ui->edit_max = max;
    ui->edit_rect = box;
    ui->edit_seen = true;
    snprintf(ui->edit, sizeof(ui->edit), "%s", initial);
    ui->edit_len = (int)strlen(ui->edit);
    ui->cursor = ui->edit_len;
    ui->select_all = true;
}

static void edit_insert(br_ui *ui, char ch) {
    if (ui->select_all) { ui->edit_len = ui->cursor = 0; ui->select_all = false; }
    if (ui->edit_len + 1 >= (int)sizeof(ui->edit)) return;
    if (ui->edit_kind == EDIT_TEXT && (size_t)ui->edit_len + 1 >= ui->edit_size) return;
    memmove(ui->edit + ui->cursor + 1, ui->edit + ui->cursor, (size_t)(ui->edit_len - ui->cursor + 1));
    ui->edit[ui->cursor++] = ch;
    ui->edit_len++;
}

static void edit_erase(br_ui *ui, int at) {   /* remove the character at `at` */
    if (at < 0 || at >= ui->edit_len) return;
    memmove(ui->edit + at, ui->edit + at + 1, (size_t)(ui->edit_len - at));
    ui->edit_len--;
    if (ui->cursor > at) ui->cursor--;
}

/* The field being typed into. Returns true when it was committed. */
static bool edit_box(br_ui *ui, br_rect box) {
    const br_input *in = ui->in;
    int i;
    br_vec2 before;
    char tmp[BR_UI_TEXT_MAX];
    float x, text_y, caret_x;
    ui->edit_seen = true;
    ui->edit_rect = box;
    for (i = 0; i < in->text_count; ++i)
        if (in->text[i] >= 32 && in->text[i] < 127) edit_insert(ui, (char)in->text[i]);
    if (br_key_repeated(in, BR_KEY_BACKSPACE)) {
        if (ui->select_all) { ui->edit_len = ui->cursor = 0; ui->edit[0] = 0; ui->select_all = false; }
        else edit_erase(ui, ui->cursor - 1);
    }
    if (br_key_repeated(in, BR_KEY_DELETE)) {
        if (ui->select_all) { ui->edit_len = ui->cursor = 0; ui->edit[0] = 0; ui->select_all = false; }
        else edit_erase(ui, ui->cursor);
    }
    if (br_key_repeated(in, BR_KEY_LEFT) && ui->cursor > 0) { ui->cursor--; ui->select_all = false; }
    if (br_key_repeated(in, BR_KEY_RIGHT) && ui->cursor < ui->edit_len) { ui->cursor++; ui->select_all = false; }
    if (br_key_pressed(in, BR_KEY_HOME)) { ui->cursor = 0; ui->select_all = false; }
    if (br_key_pressed(in, BR_KEY_END)) { ui->cursor = ui->edit_len; ui->select_all = false; }
    ui->edit[ui->edit_len] = 0;

    box_fill(ui, box, ui->style.field, ui->style.accent);
    memcpy(tmp, ui->edit, (size_t)ui->cursor);
    tmp[ui->cursor] = 0;
    before = br_measure_text(tmp, ui->style.font);
    x = box.x + 4;
    if (before.x > box.w - 10) x -= before.x - (box.w - 10);          /* keep the caret in view */
    text_y = box.y + floorf((box.h - 8.0f * ui->style.font) / 2.0f + 0.5f);
    br_set_clip(ui->r, intersect(current_clip(ui), to_recti(inset(box, 1))));
    if (ui->select_all && ui->edit_len > 0)
        br_fill_rect(ui->r, br_rect_make(x - 1, text_y - 1, br_measure_text(ui->edit, ui->style.font).x + 2, 8 * ui->style.font + 2), ui->style.selection);
    br_draw_text(ui->r, ui->edit, floorf(x), text_y, ui->style.font, ui->style.text);
    caret_x = floorf(x + before.x);
    if (fmod(ui->time, 1.0) < 0.6) br_fill_rect(ui->r, br_rect_make(caret_x, text_y - 1, 1, 8 * ui->style.font + 2), ui->style.accent);
    restore_clip(ui);

    if (br_key_pressed(in, BR_KEY_ENTER) || br_key_pressed(in, BR_KEY_KP_ENTER) || br_key_pressed(in, BR_KEY_TAB))
        return commit_edit(ui);
    if (br_key_pressed(in, BR_KEY_ESCAPE)) ui->edit_id = 0;
    return false;
}

bool br_ui_text_field(br_ui *ui, const char *label, char *buffer, size_t size) {
    br_ui_id id = id_of(ui, label);
    br_rect v = field_value(ui, label, br_ui_row(ui, 0));
    int f;
    if (size == 0) return false;
    if (ui->edit_id == id) { ui->last_id = id; ui->last_rect = v; ui->last_flags = 0; return edit_box(ui, v); }
    f = behave(ui, id, v);
    box_fill(ui, v, (f & B_HOVER) ? br_color_lerp(ui->style.field, ui->style.button, 0.5f) : ui->style.field, ui->style.border);
    text_in(ui, buffer, strlen(buffer), v, 0, 4, ui->style.text);
    if (f & B_PRESS) begin_edit(ui, id, v, buffer, EDIT_TEXT, size, 0, 0, buffer);
    return false;
}

/* ---- numbers ------------------------------------------------------------------------- */

static void format_number(double v, int kind, char *out, size_t size) {
    size_t n;
    if (kind != EDIT_FLOAT) { snprintf(out, size, "%d", (int)floor(v + 0.5)); return; }
    snprintf(out, size, fabs(v) >= 1000 ? "%.1f" : "%.3f", v);
    n = strlen(out);
    while (n > 0 && out[n - 1] == '0') out[--n] = 0;       /* 1.500 -> 1.5, 2.000 -> 2 */
    if (n > 0 && out[n - 1] == '.') out[--n] = 0;
    if (strcmp(out, "-0") == 0) snprintf(out, size, "0");
}

/* Drag to change, click to type. */
static bool number_box(br_ui *ui, br_ui_id id, br_rect box, void *ptr, int kind, double speed,
                       double min, double max, const char *prefix) {
    char text[64], shown[80];
    bool changed = false;
    int f;
    if (ui->edit_id == id) return edit_box(ui, box);
    if (speed <= 0) speed = min < max ? (max - min) / 300.0 : kind == EDIT_FLOAT ? 0.1 : 0.25;
    f = behave(ui, id, box);
    if (f & B_PRESS) ui->press_value = read_num(ptr, kind);
    if ((f & B_HELD) && ui->active == id) {
        double dx = ui->mouse.x - ui->press_mouse.x;
        if (!ui->dragging && fabs(dx) > 3) ui->dragging = true;
        if (ui->dragging) {
            double before = read_num(ptr, kind);
            write_num(ptr, kind, ui->press_value + dx * speed, min, max);
            changed = read_num(ptr, kind) != before;
        }
    }
    format_number(read_num(ptr, kind), kind, text, sizeof(text));
    if ((f & B_CLICK) && !ui->dragging) {
        begin_edit(ui, id, box, ptr, kind, 0, min, max, text);
        return edit_box(ui, box);
    }
    box_fill(ui, box, (f & B_HOVER) ? br_color_lerp(ui->style.field, ui->style.button, 0.6f) : ui->style.field,
             ui->active == id ? ui->style.accent : ui->style.border);
    snprintf(shown, sizeof(shown), "%s%s", prefix ? prefix : "", text);
    text_in(ui, shown, strlen(shown), box, 0.5f, 2, ui->style.text);
    return changed;
}

bool br_ui_drag_float(br_ui *ui, const char *label, float *value, float speed, float min, float max) {
    br_ui_id id = id_of(ui, label);
    br_rect v = field_value(ui, label, br_ui_row(ui, 0));
    return number_box(ui, id, v, value, EDIT_FLOAT, speed, min, max, NULL);
}

bool br_ui_drag_int(br_ui *ui, const char *label, int *value, float speed, int min, int max) {
    br_ui_id id = id_of(ui, label);
    br_rect v = field_value(ui, label, br_ui_row(ui, 0));
    return number_box(ui, id, v, value, EDIT_INT, speed, min, max, NULL);
}

bool br_ui_vec2(br_ui *ui, const char *label, br_vec2 *value, float speed) {
    br_ui_id id = id_of(ui, label);
    br_rect v = field_value(ui, label, br_ui_row(ui, 0));
    float half = floorf((v.w - COLUMN_GAP) / 2);
    bool changed = number_box(ui, hash_str(id, "x"), br_rect_make(v.x, v.y, half, v.h), &value->x, EDIT_FLOAT, speed, 0, 0, "x ");
    changed |= number_box(ui, hash_str(id, "y"), br_rect_make(v.x + half + COLUMN_GAP, v.y, v.w - half - COLUMN_GAP, v.h),
                          &value->y, EDIT_FLOAT, speed, 0, 0, "y ");
    return changed;
}

bool br_ui_color(br_ui *ui, const char *label, br_color *value) {
    static const char *const keys[4] = { "r", "g", "b", "a" };
    br_ui_id id = id_of(ui, label);
    br_rect v = field_value(ui, label, br_ui_row(ui, 0));
    br_rect swatch = br_rect_make(v.x, v.y, v.h, v.h);
    float w = floorf((v.w - v.h - 2 - 3 * 2) / 4), x = v.x + v.h + 2;
    uint8_t *ch[4];
    bool changed = false;
    int i;
    ch[0] = &value->r; ch[1] = &value->g; ch[2] = &value->b; ch[3] = &value->a;
    /* checkerboard under the swatch so transparency shows */
    br_fill_rect(ui->r, swatch, br_rgb(200, 200, 200));
    br_fill_rect(ui->r, br_rect_make(swatch.x, swatch.y, swatch.w / 2, swatch.h / 2), br_rgb(120, 120, 120));
    br_fill_rect(ui->r, br_rect_make(swatch.x + swatch.w / 2, swatch.y + swatch.h / 2, swatch.w / 2, swatch.h / 2), br_rgb(120, 120, 120));
    br_fill_rect(ui->r, swatch, *value);
    br_draw_rect(ui->r, swatch, ui->style.border);
    for (i = 0; i < 4; ++i, x += w + 2)
        changed |= number_box(ui, hash_str(id, keys[i]), br_rect_make(x, v.y, i == 3 ? v.x + v.w - x : w, v.h),
                              ch[i], EDIT_U8, 1.0, 0, 255, NULL);
    return changed;
}

bool br_ui_combo(br_ui *ui, const char *label, int *index, const char *const *items, int count) {
    br_ui_id id = id_of(ui, label);
    br_rect v = field_value(ui, label, br_ui_row(ui, 0));
    bool changed = false;
    float cx, cy;
    int f;
    const char *current = (*index >= 0 && *index < count) ? items[*index] : "?";
    if (ui->popup_result_id == id) {
        changed = *index != ui->popup_result;
        *index = ui->popup_result;
        ui->popup_result_id = 0;
        current = items[*index];
    }
    f = behave(ui, id, v);
    if (f & B_PRESS) {
        if (ui->popup_id == id) {
            ui->popup_id = 0;
        } else {
            float h = ui->style.row * (float)count + 2;
            float y = v.y + v.h;
            if (y + h > (float)(ui->screen_clip.y + ui->screen_clip.h)) y = v.y - h;   /* no room below: open upwards */
            ui->popup_id = id;
            ui->popup_items = items;
            ui->popup_count = count;
            ui->popup_selected = *index;
            ui->popup_rect = br_rect_make(v.x, y, v.w, h);
            ui->popup_just_opened = true;
        }
    }
    box_fill(ui, v, button_color(ui, id, f), ui->popup_id == id ? ui->style.accent : ui->style.border);
    text_in(ui, current, strlen(current), br_rect_make(v.x, v.y, v.w - 12, v.h), 0, 4, ui->style.text);
    cx = v.x + v.w - 8;
    cy = v.y + v.h / 2;
    br_fill_triangle(ui->r, br_v2(cx - 3, cy - 2), br_v2(cx + 3, cy - 2), br_v2(cx, cy + 2), ui->style.text_dim);
    return changed;
}

/* ---- areas ------------------------------------------------------------------- */

int br_ui_area(br_ui *ui, const char *label, float height) {
    br_ui_id id = id_of(ui, label);
    br_rect r = br_ui_row(ui, height);
    return behave(ui, id, r);
}

br_rect br_ui_last_rect(const br_ui *ui) { return ui->last_rect; }
int br_ui_last_flags(const br_ui *ui) { return ui->last_flags; }

/* ---- drag and drop ------------------------------------------------------------- */

bool br_ui_drag_source(br_ui *ui, const char *type, const char *payload, const char *label) {
    br_vec2 d;
    if (ui->drag.active) return ui->drag.source == ui->last_id && ui->last_id != 0;
    if (!ui->last_id || ui->active != ui->last_id || !ui->down || ui->disabled) return false;
    d = br_v2_sub(ui->mouse, ui->press_mouse);
    if (d.x * d.x + d.y * d.y < DRAG_THRESHOLD * DRAG_THRESHOLD) return false;
    ui->drag.active = true;
    ui->drag.source = ui->last_id;
    snprintf(ui->drag.type, sizeof(ui->drag.type), "%s", type ? type : "");
    snprintf(ui->drag.payload, sizeof(ui->drag.payload), "%s", payload ? payload : "");
    snprintf(ui->drag.label, sizeof(ui->drag.label), "%s", label ? label : "");
    ui->active = DRAG_ACTIVE_ID;                    /* no other widget reacts meanwhile */
    ui->active_seen = true;
    ui->click_id = 0;
    return true;
}

const char *br_ui_dragging(const br_ui *ui, const char *type) {
    if (!ui->drag.active) return NULL;
    if (type && strcmp(type, ui->drag.type) != 0) return NULL;
    return ui->drag.payload;
}

const char *br_ui_drop_target(br_ui *ui, br_rect rect, const char *type) {
    br_rect hl;
    if (!br_ui_dragging(ui, type) || ui->disabled) return NULL;
    if (!in_rect(rect, ui->mouse) || !in_clip(current_clip(ui), ui->mouse)) return NULL;
    hl = inset(rect, -1);
    br_draw_rect(ui->r, hl, ui->style.accent);
    br_draw_rect(ui->r, inset(hl, 1), ui->style.accent);
    return ui->released || !ui->down ? ui->drag.payload : NULL;
}

void br_ui_drag_cancel(br_ui *ui) {
    if (!ui->drag.active) return;
    ui->drag.active = false;
    ui->active = 0;
}
