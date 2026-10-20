/*
 * Bronto Engine - immediate-mode GUI.
 *
 * Widgets are plain function calls made every frame, between
 * br_ui_begin() and br_ui_end(). There is no widget tree to build or keep
 * in sync: a call both draws the widget and reports what happened to it.
 *
 *   br_ui_begin(&ui, &engine->renderer, &engine->input, engine->time);
 *   br_ui_panel_begin(&ui, "settings", br_rect_make(0, 0, 200, 300));
 *       if (br_ui_button(&ui, "Reset")) reset();
 *       br_ui_drag_float(&ui, "Speed", &speed, 0.1f, 0, 10);
 *       br_ui_checkbox(&ui, "Music", &music_on);
 *   br_ui_panel_end(&ui);
 *   br_ui_end(&ui);
 *
 * Call it from a scene's render callback, so it draws over the frame.
 *
 * Layout is a vertical flow inside each panel: every widget takes the next
 * row, and br_ui_columns(n) puts the next n widgets side by side. Panels
 * scroll with the mouse wheel when their content is taller than they are.
 *
 * Widgets are told apart by their label. When two would share one, add
 * "##something" to the label: the text after ## is not shown but makes the
 * id unique. Or wrap repeated groups in br_ui_push_id() / br_ui_pop_id().
 *
 * Drag and drop: right after a widget, br_ui_drag_source() lets the mouse
 * pull it away (a few pixels of movement start the drag) carrying a type
 * and a text payload, such as a file path. Anything can be a target:
 * br_ui_drop_target(rect, type) highlights while a matching drag hovers it
 * and returns the payload on the frame the mouse is released over it.
 * Escape cancels a drag.
 *
 * Number fields: drag left or right to change the value, or click to type
 * one in. Text fields commit on Enter, Tab or a click elsewhere, and Escape
 * cancels. While typing, Ctrl+A selects everything and Ctrl+C / Ctrl+X /
 * Ctrl+V copy, cut and paste (Cmd on a Mac). They use the clipboard given to
 * br_ui_set_clipboard - the system's, for example:
 *
 *   static bool set(void *e, const char *t) { return br_clipboard_set(e, t); }
 *   static char *get(void *e) { return br_clipboard_get(e); }
 *   br_ui_clipboard cb = { set, get, engine };
 *   br_ui_set_clipboard(&ui, &cb);
 *
 * and otherwise one of their own.
 *
 * Context menus: a right-click on a widget sets BR_UI_RIGHT_CLICKED in
 * br_ui_last_flags(); br_ui_context_area() catches right-clicks on empty
 * space. Either opens a menu by name, and the menu's items are listed every
 * frame while it is open, anywhere in the frame (it is drawn on top at
 * br_ui_end):
 *
 *   if (br_ui_last_flags(&ui) & BR_UI_RIGHT_CLICKED) br_ui_menu_open(&ui, "row");
 *   ...
 *   if (br_ui_menu_begin(&ui, "row")) {
 *       if (br_ui_menu_item(&ui, "Delete", "Del", true)) delete_it();
 *       br_ui_menu_separator(&ui);
 *       if (br_ui_menu_sub_begin(&ui, "Add", true)) {        (a submenu)
 *           if (br_ui_menu_item(&ui, "Box", NULL, true)) add_box();
 *           br_ui_menu_sub_end(&ui);
 *       }
 *       br_ui_menu_end(&ui);
 *   }
 *
 * An item returns true once, on the frame after it was chosen; then the
 * menu closes. Clicking elsewhere or Escape closes it too; the arrow keys
 * and Enter work as well. While a menu is open the rest of the UI ignores
 * the mouse (br_ui_wants_mouse is true everywhere).
 */
#ifndef BRONTO_UI_H
#define BRONTO_UI_H

#include "render.h"
#include "input.h"

typedef uint32_t br_ui_id;

/* Where text fields copy to and paste from. get returns br_alloc'd text (or NULL). */
typedef struct br_ui_clipboard {
    bool (*set)(void *user, const char *text);
    char *(*get)(void *user);
    void *user;
} br_ui_clipboard;

/* What happened to a widget this frame (br_ui_area, br_ui_last_flags). */
enum {
    BR_UI_HOVERED = 1, BR_UI_PRESSED = 2, BR_UI_HELD = 4, BR_UI_CLICKED = 8,
    BR_UI_RELEASED = 16, BR_UI_DOUBLE_CLICKED = 32, BR_UI_RIGHT_CLICKED = 64
};

#define BR_UI_MENU_ITEMS 64

/* A menu item as listed this frame (internal to br_ui). */
typedef struct br_ui_menu_entry {
    char label[64];
    char shortcut[24];
    int8_t parent;                    /* the submenu it is in, -1: the menu itself */
    uint8_t flags;
} br_ui_menu_entry;

#define BR_UI_DRAG_TYPE 16
#define BR_UI_DOUBLE_CLICK_TIME 0.4

typedef struct br_ui_style {
    br_color background, panel, header, button, button_hot, button_active,
             field, text, text_dim, accent, border, selection, popup;
    float font;         /* text scale: 1 = the 8 px font */
    float row;          /* height of a widget row */
    float spacing;      /* gap between rows */
    float padding;      /* inside panels */
    float label_frac;   /* share of a row that a field's label takes */
} br_ui_style;

br_ui_style br_ui_default_style(void);

#define BR_UI_ID_STACK 16
#define BR_UI_PANEL_STACK 8
#define BR_UI_TEXT_MAX 256
#define BR_UI_SCROLL_SLOTS 32
#define BR_UI_MAX_PANELS 32

typedef struct br_ui_panel {
    br_ui_id id;
    br_rect rect;
    br_recti clip;
    float x, y;                       /* layout cursor */
    float indent;
    float bottom;                     /* lowest point used */
    int cols_left;                    /* br_ui_columns state */
    float col_x, col_w, row_y;
    float scroll;
    int scroll_slot;
    bool placed;                      /* br_ui_place: the next row is `place` */
    br_rect place;
    bool stick, was_at_end;           /* br_ui_stick_to_bottom */
} br_ui_panel;

typedef struct br_ui {
    br_ui_style style;
    br_renderer *r;
    const br_input *in;
    double time;

    br_vec2 mouse;
    bool down, pressed, released;
    bool right_pressed, right_used;   /* right_used: a menu was opened by it this frame */
    bool focus_next;                  /* br_ui_focus_next */
    float wheel;
    bool wheel_used;

    br_ui_id active;                  /* the widget the mouse is holding */
    bool active_seen;
    br_vec2 press_mouse;
    double press_value;
    bool dragging;

    /* text editing: one field at a time has the keyboard */
    br_ui_id edit_id;
    char edit[BR_UI_TEXT_MAX];
    int edit_len, cursor;
    bool select_all, edit_seen;
    void *edit_ptr;                   /* what a commit writes to */
    int edit_kind;
    size_t edit_size;
    double edit_min, edit_max;
    br_rect edit_rect;

    /* a combo box's drop-down list, drawn on top at br_ui_end */
    br_ui_id popup_id;
    br_rect popup_rect;
    const char *const *popup_items;
    int popup_count, popup_selected;
    br_ui_id popup_result_id;
    int popup_result;
    bool popup_just_opened;

    br_ui_id ids[BR_UI_ID_STACK];
    int id_depth;
    br_ui_panel panels[BR_UI_PANEL_STACK];
    int panel_depth;
    struct { br_ui_id id; float scroll, content; } scrolls[BR_UI_SCROLL_SLOTS];
    int scroll_count;
    br_rect areas[BR_UI_MAX_PANELS], areas_prev[BR_UI_MAX_PANELS];   /* panel rects */
    int area_count, area_count_prev;
    br_recti screen_clip;

    /* the widget drawn last, for br_ui_last_rect / br_ui_drag_source */
    br_ui_id last_id;
    br_rect last_rect;
    int last_flags;
    br_ui_id click_id;                /* the last click, for double clicks */
    double click_time;

    struct {
        bool active;
        br_ui_id source;
        char type[BR_UI_DRAG_TYPE];
        char payload[BR_UI_TEXT_MAX];
        char label[64];
    } drag;

    /* the open context menu: its items are listed each frame, drawn at br_ui_end */
    struct {
        br_ui_id id;                  /* 0: none open */
        br_vec2 pos;
        bool begun, opened_now;
        int result;                   /* the item chosen last frame, reported this frame */
        int hot;                      /* highlighted item (mouse or keys) */
        int sub;                      /* the open submenu's item, -1: none */
        int count, parent;            /* while listing */
        br_ui_menu_entry items[BR_UI_MENU_ITEMS];
        br_rect rect, sub_rect;       /* where it was drawn */
        br_vec2 last_mouse;           /* the mouse moved: it picks the item, not the keys */
    } menu;

    br_ui_clipboard clipboard;        /* (set: NULL = own_clip) */
    char own_clip[BR_UI_TEXT_MAX];

    bool disabled;                    /* widgets draw but don't react (behind a dialog) */
    uint32_t edits_committed;         /* counts commits (for undo / dirty tracking) */
} br_ui;

void br_ui_init(br_ui *ui);
void br_ui_begin(br_ui *ui, br_renderer *r, const br_input *in, double time);
void br_ui_end(br_ui *ui);

/* Does the GUI want this mouse position (a panel, an open list or a drag
 * is under it)? Then the game or the editor's viewport should ignore it. */
bool br_ui_wants_mouse(const br_ui *ui, br_vec2 p);
/* A text field has the keyboard: don't treat key presses as shortcuts. */
bool br_ui_wants_keyboard(const br_ui *ui);
void br_ui_set_disabled(br_ui *ui, bool disabled);

/* ---- layout ---------------------------------------------------------- */
void br_ui_panel_begin(br_ui *ui, const char *name, br_rect rect);
void br_ui_panel_end(br_ui *ui);
br_rect br_ui_row(br_ui *ui, float height);   /* reserve the next row (0 = style.row) */
void br_ui_columns(br_ui *ui, int count);     /* the next `count` widgets share one row */
void br_ui_place(br_ui *ui, br_rect rect);    /* the next widget goes exactly here (toolbars) */
void br_ui_spacing(br_ui *ui, float pixels);
void br_ui_indent(br_ui *ui, float pixels);   /* negative to undo */
void br_ui_push_id(br_ui *ui, const char *key);
void br_ui_push_id_int(br_ui *ui, int key);
void br_ui_pop_id(br_ui *ui);

/* ---- widgets ------------------------------------------------------------ */
void br_ui_label(br_ui *ui, const char *text);
void br_ui_label_colored(br_ui *ui, const char *text, br_color color);
void br_ui_labelf(br_ui *ui, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
void br_ui_heading(br_ui *ui, const char *text);
void br_ui_separator(br_ui *ui);

bool br_ui_button(br_ui *ui, const char *label);
bool br_ui_toggle(br_ui *ui, const char *label, bool on);          /* a button that stays lit */
bool br_ui_checkbox(br_ui *ui, const char *label, bool *value);
bool br_ui_selectable(br_ui *ui, const char *label, bool selected); /* a list row */
/* A section header; returns *open. With `remove`, an x on the right sets it. */
bool br_ui_header(br_ui *ui, const char *label, bool *open, bool *remove);

/* Return true when the value changed this frame. min == max: no limits. */
bool br_ui_drag_float(br_ui *ui, const char *label, float *value, float speed, float min, float max);
bool br_ui_drag_int(br_ui *ui, const char *label, int *value, float speed, int min, int max);
bool br_ui_vec2(br_ui *ui, const char *label, br_vec2 *value, float speed);
bool br_ui_color(br_ui *ui, const char *label, br_color *value);
bool br_ui_text_field(br_ui *ui, const char *label, char *buffer, size_t size);
bool br_ui_combo(br_ui *ui, const char *label, int *index, const char *const *items, int count);

/* An invisible clickable box taking the next row (`height` 0 = style.row),
 * for things the caller draws itself (tiles, thumbnails). Returns
 * BR_UI_* flags; draw into br_ui_last_rect(). */
int br_ui_area(br_ui *ui, const char *label, float height);
br_rect br_ui_last_rect(const br_ui *ui);
int br_ui_last_flags(const br_ui *ui);   /* the flags of the last clickable widget */

/* ---- drag and drop ------------------------------------------------------- */
/* Call right after the widget to drag. Returns true while it is being dragged.
 * `label` is shown next to the mouse (NULL: the payload). */
bool br_ui_drag_source(br_ui *ui, const char *type, const char *payload, const char *label);
/* The payload of the drag in progress if it has this type (NULL type: any), else NULL. */
const char *br_ui_dragging(const br_ui *ui, const char *type);
/* The payload when a drag of `type` is released over `rect` (inside the
 * current panel's visible area), else NULL. Highlights the rect while hovered. */
const char *br_ui_drop_target(br_ui *ui, br_rect rect, const char *type);
void br_ui_drag_cancel(br_ui *ui);

/* ---- context menus (see the top of this file) --------------------------- */
/* A right-click on `rect` this frame that no widget or menu took (empty space). */
bool br_ui_context_area(br_ui *ui, br_rect rect);
void br_ui_menu_open(br_ui *ui, const char *name);      /* at the mouse */
void br_ui_menu_open_at(br_ui *ui, const char *name, br_vec2 pos);   /* e.g. under a toolbar button */
void br_ui_menu_close(br_ui *ui);
bool br_ui_menu_is_open(const br_ui *ui, const char *name);
bool br_ui_menu_active(const br_ui *ui);                /* any menu open: keys belong to it */
bool br_ui_menu_begin(br_ui *ui, const char *name);     /* true while it is open: list the items */
bool br_ui_menu_item(br_ui *ui, const char *label, const char *shortcut, bool enabled);
bool br_ui_menu_check(br_ui *ui, const char *label, bool checked);   /* an item with a tick */
void br_ui_menu_separator(br_ui *ui);
bool br_ui_menu_sub_begin(br_ui *ui, const char *label, bool enabled);   /* always true: list its items */
void br_ui_menu_sub_end(br_ui *ui);
void br_ui_menu_end(br_ui *ui);

/* ---- odds and ends ---------------------------------------------------------- */
/* The next text field drawn starts editing, all selected (a Rename command). */
void br_ui_focus_next(br_ui *ui);
/* The clipboard for Ctrl+C / Ctrl+X / Ctrl+V in text fields (NULL: the GUI's own). */
void br_ui_set_clipboard(br_ui *ui, const br_ui_clipboard *clipboard);
/* In a panel: if it was scrolled to the end, keep it there as it grows (logs). */
void br_ui_stick_to_bottom(br_ui *ui);

#endif /* BRONTO_UI_H */
