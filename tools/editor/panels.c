/*
 * BRONTO EDITOR - the panels around the scene view.
 *
 *   toolbar     New / Open / Save, the file name, Undo / Redo, Play, Grid / Snap
 *   hierarchy   every entity in the scene, in order
 *   inspector   the selected entity's components, generated from their
 *               descriptions, so any described component is editable
 *   status bar  what just happened, and a few numbers
 *   console     captured output, the log and print(), with filters (Files | Console tabs)
 *   dialog      "save your changes?" before New, Open or Quit
 */
#include "editor.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- toolbar ------------------------------------------------------------------ */

/* Lays out buttons left to right. */
typedef struct strip { br_ui *ui; float x, y, h; } strip;

static br_rect strip_next(strip *s, float w) {
    br_rect r = br_rect_make(s->x, s->y, w, s->h);
    s->x += w + 4;
    return r;
}

static bool strip_button(strip *s, const char *label, float w, bool lit) {
    br_ui_place(s->ui, strip_next(s, w));
    return br_ui_toggle(s->ui, label, lit);
}

void ed_toolbar(editor *ed, br_rect rect) {
    br_ui *ui = &ed->ui;
    strip s;
    br_rect path;
    br_ui_panel_begin(ui, "toolbar", rect);
    s.ui = ui;
    s.x = rect.x + 6;
    s.y = rect.y + (rect.h - ui->style.row) / 2;
    s.h = ui->style.row;
    if (strip_button(&s, "Load Project", 100, false)) ed_cmd_load_project(ed);
    s.x += 8;
    if (strip_button(&s, "New", 40, false)) ed_cmd_new(ed);
    if (strip_button(&s, "Open", 44, false)) ed_cmd_open(ed);
    if (strip_button(&s, "Save", 44, false)) ed_cmd_save(ed);
    path = strip_next(&s, 230);
    br_ui_place(ui, path);
    br_ui_text_field(ui, "##path", ed->path_field, sizeof(ed->path_field));
    if (!ed->path_field[0] && !br_ui_wants_keyboard(ui))
        br_draw_text(ui->r, "file name.scene", path.x + 4, path.y + 4, 1, ui->style.text_dim);
    s.x += 8;
    if (strip_button(&s, "Undo", 44, false)) ed_cmd_undo(ed);
    if (strip_button(&s, "Redo", 44, false)) ed_cmd_redo(ed);
    s.x += 8;
    if (strip_button(&s, ed->doc.playing ? "Stop" : "Play", 52, ed->doc.playing)) ed_cmd_play(ed);
    s.x += 8;
    if (strip_button(&s, "Grid", 40, ed->view.grid)) ed->view.grid = !ed->view.grid;
    if (strip_button(&s, "Snap", 40, ed->view.snap)) ed->view.snap = !ed->view.snap;
    if (strip_button(&s, "Colliders", 76, ed->show_colliders)) ed->show_colliders = !ed->show_colliders;
    s.x += 8;
    {   /* Build: a menu under the button */
        br_rect b = strip_next(&s, 56);
        br_ui_place(ui, b);
        if (br_ui_toggle(ui, "Build", br_ui_menu_is_open(ui, "build"))) {
            if (br_ui_menu_is_open(ui, "build")) br_ui_menu_close(ui);
            else br_ui_menu_open_at(ui, "build", br_v2(b.x, b.y + b.h + 1));
        }
    }
    if (strip_button(&s, "Settings", 68, ed->settings_visible)) ed_settings_open(ed);
    {
        const char *title = "BRONTO EDITOR";
        br_vec2 m = br_measure_text(title, 1);
        float tx = rect.x + rect.w - m.x - 10;
        if (s.x + 20 < tx - (float)ed->badge.width) {
            br_draw_text(ui->r, title, tx, s.y + 4, 1, ui->style.text_dim);
            if (ed->badge.pixels)
                br_draw_image(ui->r, &ed->badge, NULL,
                              br_rect_make(floorf(tx - (float)ed->badge.width - 6), floorf(rect.y + (rect.h - (float)ed->badge.height) / 2),
                                           (float)ed->badge.width, (float)ed->badge.height), BR_WHITE);
        }
    }
    br_ui_panel_end(ui);
}

/* ---- hierarchy --------------------------------------------------------------------- */

#define ED_LIST_ROWS 4096
#define TREE_INDENT 12.0f

typedef struct tree_row { br_rect r; br_entity e; float indent; bool open_parent; } tree_row;

bool ed_collapsed(const editor *ed, br_entity e) {
    int i;
    for (i = 0; i < ed->collapsed_count; ++i) if (ed->collapsed[i] == e) return true;
    return false;
}

void ed_set_collapsed(editor *ed, br_entity e, bool collapsed) {
    int i;
    for (i = 0; i < ed->collapsed_count; ++i)
        if (ed->collapsed[i] == e) {
            if (!collapsed) ed->collapsed[i] = ed->collapsed[--ed->collapsed_count];
            return;
        }
    if (collapsed && ed->collapsed_count < ED_MAX_COLLAPSED) ed->collapsed[ed->collapsed_count++] = e;
}

static bool hidden_by_collapse(const editor *ed, br_entity e) {
    br_entity p = br_parent_of(&ed->doc.world, e);
    int guard = 0;
    while (p != BR_ENTITY_NULL && guard++ < 64) {
        if (ed_collapsed(ed, p)) return true;
        p = br_parent_of(&ed->doc.world, p);
    }
    return false;
}

/* Where a dragged row would go with the mouse at y: before a row, into it,
 * after it, or at the very end. Returns false if that isn't allowed. */
typedef struct tree_target { br_entity parent, before; br_rect mark; bool into; } tree_target;

static bool tree_target_at(const editor *ed, const tree_row *rows, uint32_t n, br_entity dragged, float y, tree_target *t) {
    const br_world *w = &ed->doc.world;
    uint32_t i;
    memset(t, 0, sizeof(*t));
    if (n == 0) return false;
    if (y < rows[0].r.y) i = 0;
    else for (i = 0; i < n && y >= rows[i].r.y + rows[i].r.h + 3; ++i) {}
    if (i >= n) {                                          /* below everything: the last root */
        t->parent = BR_ENTITY_NULL;
        t->before = BR_ENTITY_NULL;
        t->mark = br_rect_make(rows[0].r.x, rows[n - 1].r.y + rows[n - 1].r.h + 1, rows[0].r.w, 2);
    } else {
        const tree_row *row = &rows[i];
        float frac = (y - row->r.y) / (row->r.h > 0 ? row->r.h : 1);
        if (frac < 0.3f) {                                 /* before this row, as its sibling */
            t->parent = br_parent_of(w, row->e);
            t->before = row->e;
            t->mark = br_rect_make(row->r.x + row->indent, row->r.y - 2, row->r.w - row->indent, 2);
        } else if (frac > 0.7f && !(row->open_parent)) {   /* after it (and its subtree), as its sibling */
            uint32_t k;
            t->parent = br_parent_of(w, row->e);
            t->before = BR_ENTITY_NULL;
            for (k = i + 1; k < n; ++k)
                if (!br_is_ancestor(w, row->e, rows[k].e)) {
                    if (br_parent_of(w, rows[k].e) == t->parent) t->before = rows[k].e;
                    break;
                }
            t->mark = br_rect_make(row->r.x + row->indent, row->r.y + row->r.h + 1, row->r.w - row->indent, 2);
        } else if (frac > 0.7f) {                           /* an open parent: its first child */
            t->parent = row->e;
            t->before = i + 1 < n ? rows[i + 1].e : BR_ENTITY_NULL;
            t->mark = br_rect_make(row->r.x + row->indent + TREE_INDENT, row->r.y + row->r.h + 1, row->r.w - row->indent - TREE_INDENT, 2);
        } else {                                           /* into it, as its last child */
            t->parent = row->e;
            t->before = BR_ENTITY_NULL;
            t->into = true;
            t->mark = row->r;
        }
    }
    if (t->parent == dragged || (t->parent != BR_ENTITY_NULL && br_is_ancestor(w, dragged, t->parent))) return false;
    return true;
}

/* While a row is dragged: show where it would land; on release, move it there. */
static void tree_drop(editor *ed, br_rect panel, const tree_row *rows, uint32_t n) {
    br_ui *ui = &ed->ui;
    const char *payload = br_ui_dragging(ui, ED_ENTITY_DRAG), *dropped;
    br_entity dragged;
    tree_target t;
    bool ok;
    if (!payload) return;
    dragged = (br_entity)strtoul(payload, NULL, 10);
    ok = tree_target_at(ed, rows, n, dragged, ui->mouse.y, &t);
    if (ui->mouse.x >= panel.x && ui->mouse.x < panel.x + panel.w && ui->mouse.y >= panel.y && ui->mouse.y < panel.y + panel.h && ok) {
        if (t.into) br_draw_rect(ui->r, t.mark, ui->style.accent);
        else br_fill_rect(ui->r, t.mark, ui->style.accent);
    }
    if ((dropped = br_ui_drop_target(ui, panel, ED_ENTITY_DRAG)) != NULL && ok) {
        if (ed_doc_place(&ed->doc, dragged, t.parent, t.before)) {
            char a[64], b[64];
            if (t.parent != BR_ENTITY_NULL) ed_set_collapsed(ed, t.parent, false);
            ed->doc.selected = dragged;
            if (t.into) ed_status(ed, "%s is now a child of %s", ed_doc_name(&ed->doc, dragged, a, sizeof(a)), ed_doc_name(&ed->doc, t.parent, b, sizeof(b)));
        }
    }
}

static void draw_fold(br_ui *ui, br_rect r, bool open, bool hot) {
    float cx = floorf(r.x + r.w / 2), cy = floorf(r.y + r.h / 2);
    br_color c = hot ? ui->style.text : ui->style.text_dim;
    if (open) br_fill_triangle(ui->r, br_v2(cx - 4, cy - 2), br_v2(cx + 4, cy - 2), br_v2(cx, cy + 3), c);
    else br_fill_triangle(ui->r, br_v2(cx - 2, cy - 4), br_v2(cx + 3, cy), br_v2(cx - 2, cy + 4), c);
}

void ed_hierarchy(editor *ed, br_rect rect) {
    br_ui *ui = &ed->ui;
    ed_doc *d = &ed->doc;
    uint32_t i, n = 0;
    char name[64];
    static tree_row rows[ED_LIST_ROWS];
    br_ui_panel_begin(ui, "hierarchy", rect);
    br_ui_heading(ui, "Scene");
    br_ui_columns(ui, 2);
    if (br_ui_button(ui, "+ New")) ed_cmd_add_entity(ed);
    if (br_ui_button(ui, "+ Child")) ed_cmd_add_child(ed);
    br_ui_columns(ui, 2);
    if (br_ui_button(ui, "Copy")) ed_cmd_duplicate(ed);
    if (br_ui_button(ui, "Delete")) ed_cmd_delete(ed);
    br_ui_separator(ui);
    for (i = 0; i < d->count; ++i) {                     /* the tree: drag rows to move or reparent them */
        br_entity e = d->order[i];
        char handle[16];
        br_rect row, fold, label;
        float indent;
        bool kids;
        if (hidden_by_collapse(ed, e)) continue;
        indent = TREE_INDENT * (float)br_depth(&d->world, e);
        kids = i + 1 < d->count && br_parent_of(&d->world, d->order[i + 1]) == e;
        row = br_ui_row(ui, 0);
        fold = br_rect_make(row.x + indent, row.y, TREE_INDENT, row.h);
        label = br_rect_make(row.x + indent + TREE_INDENT, row.y, row.w - indent - TREE_INDENT, row.h);
        br_ui_push_id_int(ui, (int)e);
        if (kids) {                                      /* the open / closed arrow */
            int f;
            br_ui_place(ui, fold);
            f = br_ui_area(ui, "##fold", 0);
            draw_fold(ui, fold, !ed_collapsed(ed, e), (f & BR_UI_HOVERED) != 0);
            if (f & BR_UI_CLICKED) ed_set_collapsed(ed, e, !ed_collapsed(ed, e));
        }
        br_ui_place(ui, label);
        if (br_ui_selectable(ui, ed_doc_name(d, e, name, sizeof(name)), e == d->selected)) d->selected = e;
        if (br_ui_last_flags(ui) & BR_UI_DOUBLE_CLICKED) ed_cmd_frame(ed);
        if (br_ui_last_flags(ui) & BR_UI_RIGHT_CLICKED) ed_menu_entity(ed, e);
        snprintf(handle, sizeof(handle), "%u", (unsigned)e);
        br_ui_drag_source(ui, ED_ENTITY_DRAG, handle, name);
        br_ui_pop_id(ui);
        if (n < ED_LIST_ROWS) {
            rows[n].r = row;
            rows[n].e = e;
            rows[n].indent = indent;
            rows[n].open_parent = kids && !ed_collapsed(ed, e);
            ++n;
        }
    }
    tree_drop(ed, rect, rows, n);
    if (d->count == 0) {
        br_ui_label_colored(ui, "The scene is empty.", ui->style.text_dim);
        br_ui_label_colored(ui, "Press + New to add", ui->style.text_dim);
        br_ui_label_colored(ui, "an entity.", ui->style.text_dim);
    } else {
        br_ui_spacing(ui, 6);
        br_ui_label_colored(ui, "Drag onto a row: child", ui->style.text_dim);
        br_ui_label_colored(ui, "Drag between rows: order", ui->style.text_dim);
        br_ui_label_colored(ui, "Right-click: more", ui->style.text_dim);
    }
    if (br_ui_context_area(ui, rect)) ed_menu_hierarchy(ed);
    br_ui_panel_end(ui);
}

/* ---- inspector ----------------------------------------------------------------------- */

static float drag_speed(const br_field *f) {
    if (f->min < f->max) {
        float s = (f->max - f->min) / 300.0f;
        return s > 1.0f ? 1.0f : s;
    }
    return strcmp(f->name, "scale") == 0 ? 0.01f : f->type == BR_FIELD_INT ? 0.25f : 0.5f;
}

/* One widget per field, chosen by the field's type. */
static void edit_field(editor *ed, const br_component_desc *desc, const br_field *f, void *component) {
    br_ui *ui = &ed->ui;
    void *p = (unsigned char *)component + f->offset;
    switch (f->type) {
    case BR_FIELD_INT:
        br_ui_drag_int(ui, f->name, (int *)p, drag_speed(f), (int)f->min, (int)f->max);
        break;
    case BR_FIELD_FLOAT:
        br_ui_drag_float(ui, f->name, (float *)p, drag_speed(f), f->min, f->max);
        break;
    case BR_FIELD_BOOL:   br_ui_checkbox(ui, f->name, (bool *)p); break;
    case BR_FIELD_VEC2:   br_ui_vec2(ui, f->name, (br_vec2 *)p, drag_speed(f)); break;
    case BR_FIELD_COLOR:  br_ui_color(ui, f->name, (br_color *)p); break;
    case BR_FIELD_STRING: {
        const char *rel = br_ui_dragging(ui, ED_ASSET_DRAG), *dropped;
        br_ui_text_field(ui, f->name, (char *)p, f->size);
        if (rel && ed_field_accepts(desc, f, rel) && (dropped = br_ui_drop_target(ui, br_ui_last_rect(ui), ED_ASSET_DRAG)) != NULL)
            snprintf((char *)p, f->size, "%s", dropped);
        break;
    }
    case BR_FIELD_ENUM:   br_ui_combo(ui, f->name, (int *)p, f->enum_names, f->enum_count); break;
    case BR_FIELD_ENTITY: {                       /* drag a hierarchy row here; double-click clears */
        br_entity ref;
        char label[96], name[64];
        const char *dropped;
        br_rect r;
        int flags;
        memcpy(&ref, p, sizeof(ref));
        if (br_entity_alive(&ed->doc.world, ref)) snprintf(label, sizeof(label), "%s: %s", f->name, ed_doc_name(&ed->doc, ref, name, sizeof(name)));
        else snprintf(label, sizeof(label), "%s: none (drag an entity here)", f->name);
        flags = br_ui_area(ui, f->name, 0);
        r = br_ui_last_rect(ui);
        br_fill_rect(ui->r, r, (flags & BR_UI_HOVERED) ? ui->style.header : ui->style.field);
        br_draw_text(ui->r, label, r.x + 4, r.y + 4, 1, ui->style.text);
        if (flags & BR_UI_DOUBLE_CLICKED) { ref = BR_ENTITY_NULL; memcpy(p, &ref, sizeof(ref)); }
        if ((dropped = br_ui_drop_target(ui, r, ED_ENTITY_DRAG)) != NULL) {
            ref = (br_entity)strtoul(dropped, NULL, 10);
            memcpy(p, &ref, sizeof(ref));
        }
        break;
    }
    }
}

/* "Add component": every described component the entity doesn't have yet. */
static void add_component_menu(editor *ed, br_entity e) {
    static const char *items[BR_MAX_COMPONENTS + 1];
    br_component ids[BR_MAX_COMPONENTS + 1];
    br_world *w = &ed->doc.world;
    uint32_t c;
    int n = 0;
    items[n++] = "+ Add component";
    for (c = 0; c < br_component_type_count(w); ++c) {
        const br_component_desc *desc = br_component_get_desc(w, c);
        if (!desc || br_has(w, e, c) || strcmp(desc->name, "Parent") == 0) continue;
        ids[n] = c;
        items[n++] = desc->name;
    }
    if (n == 1) return;
    ed->add_choice = 0;
    if (br_ui_combo(&ed->ui, "##add", &ed->add_choice, items, n) && ed->add_choice > 0) {
        br_add(w, e, ids[ed->add_choice], NULL);
        ed->section_closed[ids[ed->add_choice]] = false;
        ed_status(ed, "Added %s", items[ed->add_choice]);
        ed->add_choice = 0;
    }
}

void ed_inspector(editor *ed, br_rect rect) {
    br_ui *ui = &ed->ui;
    ed_doc *d = &ed->doc;
    br_world *w = &d->world;
    br_entity e = d->selected;
    br_name *name;
    uint32_t c;
    br_component parent_id = br_component_find(&ed->doc.world, "Parent");
    br_ui_panel_begin(ui, "inspector", rect);
    br_ui_heading(ui, "Inspector");
    if (!br_entity_alive(w, e)) {
        br_ui_label_colored(ui, "Nothing selected.", ui->style.text_dim);
        br_ui_spacing(ui, 4);
        br_ui_label_colored(ui, "Click something in the scene,", ui->style.text_dim);
        br_ui_label_colored(ui, "or in the list on the left.", ui->style.text_dim);
        br_ui_panel_end(ui);
        return;
    }
    br_ui_push_id_int(ui, (int)e);         /* edits never leak from one entity to another */
    if ((name = (br_name *)br_get(w, e, d->ids.name)) != NULL) {
        if (ed->focus_name) br_ui_focus_next(ui);          /* Rename */
        br_ui_text_field(ui, "Name", name->text, sizeof(name->text));
    }
    ed->focus_name = false;
    if (br_parent_of(w, e) != BR_ENTITY_NULL) {
        char pn[64], line[96];
        snprintf(line, sizeof(line), "Child of %s", ed_doc_name(d, br_parent_of(w, e), pn, sizeof(pn)));
        br_ui_columns(ui, 2);
        br_ui_label_colored(ui, line, ui->style.text_dim);
        if (br_ui_button(ui, "Unparent")) ed_cmd_unparent(ed);
    }
    br_ui_spacing(ui, 4);
    for (c = 0; c < br_component_type_count(w); ++c) {
        const br_component_desc *desc = br_component_get_desc(w, c);
        bool open, remove = false;
        int i;
        void *data;
        if (!desc || c == d->ids.name || c == parent_id || !(data = br_get(w, e, c))) continue;   /* the tree is edited in the hierarchy */
        open = !ed->section_closed[c];
        br_ui_push_id(ui, desc->name);
        br_ui_header(ui, desc->name, &open, &remove);
        if (br_ui_last_flags(ui) & BR_UI_RIGHT_CLICKED) ed_menu_component(ed, c, -1);
        if (open) {
            br_ui_indent(ui, 6);
            for (i = 0; i < desc->field_count; ++i) {
                br_rect last;
                edit_field(ed, desc, &desc->fields[i], br_get(w, e, c));
                last = br_ui_last_rect(ui);                   /* right-click anywhere on the field's row */
                if (br_ui_context_area(ui, br_rect_make(rect.x, last.y, rect.w, last.h))) ed_menu_component(ed, c, i);
            }
            br_ui_indent(ui, -6);
            br_ui_spacing(ui, 4);
        }
        ed->section_closed[c] = !open;
        br_ui_pop_id(ui);
        if (remove) {
            br_remove(w, e, c);
            ed_status(ed, "Removed %s", desc->name);
        }
    }
    br_ui_spacing(ui, 4);
    add_component_menu(ed, e);
    br_ui_pop_id(ui);
    if (br_ui_context_area(ui, rect)) ed_menu_inspector(ed);
    br_ui_panel_end(ui);
}

/* ---- status bar ------------------------------------------------------------------------ */

void ed_statusbar(editor *ed, br_rect rect) {
    br_ui *ui = &ed->ui;
    char info[ED_PATH_MAX + 160], file[ED_PATH_MAX + 8];
    bool fresh = ed->engine->time - ed->status_time < 6.0 && ed->status[0];
    br_vec2 size;
    br_ui_panel_begin(ui, "status", rect);
    br_draw_text(ui->r, fresh ? ed->status : "Wheel: zoom   Right-drag: pan   Right-click: menu   F5: play   F: frame selection",
                 rect.x + 8, rect.y + (rect.h - 8) / 2, 1, fresh ? ui->style.text : ui->style.text_dim);
    snprintf(file, sizeof(file), "%s%s", ed->doc.path[0] ? ed->doc.path : "untitled", ed_doc_dirty(&ed->doc) ? " *" : "");
    snprintf(info, sizeof(info), "%s   %u entities   %d%%   x %.0f  y %.0f   %.0f fps", file, ed->doc.count,
             (int)(ed->view.zoom * 100 + 0.5f), (double)ed->view.mouse_world.x, (double)ed->view.mouse_world.y, ed->engine->fps);
    size = br_measure_text(info, 1);
    br_draw_text(ui->r, info, rect.x + rect.w - size.x - 8, rect.y + (rect.h - 8) / 2, 1, ui->style.text_dim);
    br_ui_panel_end(ui);
}

/* ---- the unsaved-changes dialog -------------------------------------------------------- */

void ed_dialog(editor *ed, br_rect screen) {
    br_ui *ui = &ed->ui;
    br_rect box = br_rect_make(floorf(screen.x + (screen.w - 440) / 2), floorf(screen.y + (screen.h - 108) / 2), 440, 108);
    char question[ED_PATH_MAX + 40];
    const char *verb = ed->pending == ED_PENDING_QUIT ? "quitting" : ed->pending == ED_PENDING_NEW ? "starting a new scene"
                     : ed->pending == ED_PENDING_PROJECT ? "switching projects" : "opening another";
    if (ed->pending == ED_PENDING_NONE) return;
    br_fill_rect(ui->r, screen, br_rgba(0, 0, 0, 140));
    br_ui_set_disabled(ui, false);
    br_ui_panel_begin(ui, "dialog", box);
    br_ui_heading(ui, "Unsaved changes");
    snprintf(question, sizeof(question), "Save %s before %s?", ed->doc.path[0] ? ed->doc.path : "this scene", verb);
    br_ui_label(ui, question);
    br_ui_spacing(ui, 12);
    br_ui_columns(ui, 3);
    if (br_ui_button(ui, "Save")) ed_cmd_answer(ed, 0);
    if (br_ui_button(ui, "Don't save")) ed_cmd_answer(ed, 1);
    if (br_ui_button(ui, "Cancel")) ed_cmd_answer(ed, 2);
    br_draw_rect(ui->r, box, ui->style.accent);
    br_ui_panel_end(ui);
}

/* ---- the built-in folder picker (when the OS has no dialog) ---------------------------- */

void ed_picker_close(editor *ed) {
    ed->picker.open = false;
    br_dir_list_free(&ed->picker.list);
}

static void picker_go(editor *ed, const char *dir) {
    char abs[ED_PATH_MAX];
    br_dir_list l;
    if (!br_fs_absolute(dir, abs, sizeof(abs)) || !br_fs_list(abs, false, &l)) { ed_status(ed, "Can't open %s", dir); return; }
    br_dir_list_free(&ed->picker.list);
    ed->picker.list = l;
    snprintf(ed->picker.dir, sizeof(ed->picker.dir), "%s", abs);
}

void ed_picker_dialog(editor *ed, br_rect screen) {
    br_ui *ui = &ed->ui;
    ed_picker *p = &ed->picker;
    br_rect box = br_rect_make(floorf(screen.x + (screen.w - 480) / 2), floorf(screen.y + (screen.h - 380) / 2), 480, 380);
    char go[ED_PATH_MAX];
    int i, shown = 0;
    if (!p->open) return;
    go[0] = 0;
    br_fill_rect(ui->r, screen, br_rgba(0, 0, 0, 140));
    br_ui_set_disabled(ui, false);
    br_ui_panel_begin(ui, "picker", box);
    br_ui_heading(ui, "Choose the project folder");
    br_ui_label_colored(ui, p->dir, ui->style.text_dim);
    br_ui_columns(ui, 3);
    if (br_ui_button(ui, "Up##picker")) br_path_parent(go, sizeof(go), p->dir);
    if (br_ui_button(ui, "Open this folder")) {
        char chosen[ED_PATH_MAX];
        snprintf(chosen, sizeof(chosen), "%s", p->dir);
        ed_picker_close(ed);
        br_ui_panel_end(ui);
        ed_choose_project(ed, chosen);
        return;
    }
    if (br_ui_button(ui, "Cancel##picker")) { ed_picker_close(ed); br_ui_panel_end(ui); return; }
    br_ui_separator(ui);
    for (i = 0; i < p->list.count; ++i) {
        char label[300];
        if (!p->list.entries[i].is_dir) continue;
        snprintf(label, sizeof(label), "%.250s/##pick%d", p->list.entries[i].name, i);
        if (br_ui_selectable(ui, label, false)) br_path_join(go, sizeof(go), p->dir, p->list.entries[i].name);
        ++shown;
    }
    if (!shown) br_ui_label_colored(ui, "No folders here.", ui->style.text_dim);
    br_draw_rect(ui->r, box, ui->style.accent);
    br_ui_panel_end(ui);
    if (go[0]) picker_go(ed, go);
    if (br_key_pressed(&ed->engine->input, BR_KEY_ESCAPE)) ed_picker_close(ed);
}

/* ---- under the scene view: Files | Console ------------------------------------------------ */

void ed_bottom_tabs(editor *ed, br_rect rect) {
    br_ui *ui = &ed->ui;
    strip s;
    char label[64];
    uint32_t errors = ed->console.level_count[ED_LEVEL_ERROR];
    uint32_t fresh = errors > ed->console_errors_seen ? errors - ed->console_errors_seen : 0;
    br_ui_panel_begin(ui, "tabs", rect);
    br_fill_rect(ui->r, rect, ui->style.background);
    s.ui = ui;
    s.x = rect.x + 4;
    s.y = rect.y + (rect.h - ui->style.row) / 2;
    s.h = ui->style.row;
    if (strip_button(&s, "Files", 60, ed->bottom_tab == ED_TAB_FILES)) ed->bottom_tab = ED_TAB_FILES;
    if (fresh && ed->bottom_tab != ED_TAB_CONSOLE) snprintf(label, sizeof(label), "Console (%u error%s)##tab", fresh, fresh == 1 ? "" : "s");
    else snprintf(label, sizeof(label), "Console##tab");
    if (strip_button(&s, label, fresh && ed->bottom_tab != ED_TAB_CONSOLE ? 150 : 76, ed->bottom_tab == ED_TAB_CONSOLE)) ed->bottom_tab = ED_TAB_CONSOLE;
    if (fresh && ed->bottom_tab != ED_TAB_CONSOLE)                 /* a red mark on the tab */
        br_fill_rect(ui->r, br_rect_make(s.x - 4 - 150 + 3, s.y + 3, 3, s.h - 6), br_rgb(255, 110, 100));
    br_ui_panel_end(ui);
    if (ed->bottom_tab == ED_TAB_CONSOLE) {
        ed->console_seen = ed->console.added;
        ed->console_errors_seen = errors;
    } else if (ed->console_errors_seen > errors) {
        ed->console_errors_seen = errors;                          /* cleared meanwhile */
    }
}

#define CONSOLE_LINE_H 13.0f

static br_color line_color(const ed_con_line *l) {
    if (l->level == ED_LEVEL_ERROR) return br_rgb(255, 125, 115);
    if (l->level == ED_LEVEL_WARN) return br_rgb(240, 205, 95);
    switch (l->kind) {
    case ED_CON_PRINT: return br_rgb(150, 205, 255);
    case ED_CON_ERR:   return br_rgb(240, 170, 120);
    case ED_CON_INFO:  return br_rgb(154, 163, 178);
    default:           return br_rgb(230, 233, 239);
    }
}

static bool matches(const char *text, const char *search) {
    size_t n = strlen(search);
    if (!n) return true;
    for (; *text; ++text) {
        size_t i;
        for (i = 0; i < n && text[i] && tolower((unsigned char)text[i]) == tolower((unsigned char)search[i]); ++i) {}
        if (i == n) return true;
    }
    return false;
}

static void open_line(editor *ed, uint32_t index) {
    const ed_con_line *l = ed_console_line(&ed->console, index);
    char file[ED_PATH_MAX];
    int line;
    if (l && ed_console_location(l->text, file, sizeof(file), &line)) ed_open_script_named(ed, file, line);
}

void ed_console_panel(editor *ed, br_rect rect) {
    static uint32_t shown[ED_CONSOLE_LINES];
    br_ui *ui = &ed->ui;
    ed_console *c = &ed->console;
    br_rect bar = br_rect_make(rect.x, rect.y, rect.w, 24), list = br_rect_make(rect.x, rect.y + 24, rect.w, rect.h - 24), rows;
    char label[48];
    uint32_t i, n = 0;
    int f;
    strip s;
    /* the bar: clear, the three filters with their counts, search */
    br_ui_panel_begin(ui, "console_bar", bar);
    s.ui = ui;
    s.x = bar.x + 6;
    s.y = bar.y + 4;
    s.h = ui->style.row;
    if (strip_button(&s, "Clear##console", 50, false)) { ed_console_clear(c); ed->console_selected = -1; }
    s.x += 6;
    snprintf(label, sizeof(label), "Messages %u##f0", c->level_count[ED_LEVEL_LOG]);
    if (strip_button(&s, label, 104, ed->console_show[ED_LEVEL_LOG])) ed->console_show[ED_LEVEL_LOG] = !ed->console_show[ED_LEVEL_LOG];
    snprintf(label, sizeof(label), "Warnings %u##f1", c->level_count[ED_LEVEL_WARN]);
    if (strip_button(&s, label, 104, ed->console_show[ED_LEVEL_WARN])) ed->console_show[ED_LEVEL_WARN] = !ed->console_show[ED_LEVEL_WARN];
    snprintf(label, sizeof(label), "Errors %u##f2", c->level_count[ED_LEVEL_ERROR]);
    if (strip_button(&s, label, 88, ed->console_show[ED_LEVEL_ERROR])) ed->console_show[ED_LEVEL_ERROR] = !ed->console_show[ED_LEVEL_ERROR];
    s.x += 6;
    if (strip_button(&s, "Clear on Play", 108, ed->console_clear_on_play)) ed->console_clear_on_play = !ed->console_clear_on_play;
    {
        float w = bar.x + bar.w - 6 - s.x;
        if (w > 220) { s.x = bar.x + bar.w - 6 - 220; w = 220; }
        if (w > 60) {
            br_rect box = strip_next(&s, w);
            br_ui_place(ui, box);
            br_ui_text_field(ui, "##search", ed->console_search, sizeof(ed->console_search));
            if (!ed->console_search[0] && !br_ui_wants_keyboard(ui))
                br_draw_text(ui->r, "search", box.x + 4, box.y + 4, 1, ui->style.text_dim);
        }
    }
    br_ui_panel_end(ui);

    /* the lines: only the visible ones are drawn; one area takes the clicks */
    for (i = 0; i < c->count; ++i) {
        const ed_con_line *l = ed_console_line(c, i);
        if (ed->console_show[l->level] && matches(l->text, ed->console_search)) shown[n++] = i;
    }
    br_ui_panel_begin(ui, "console_list", list);
    br_fill_rect(ui->r, list, ui->style.field);
    br_ui_stick_to_bottom(ui);
    if (n == 0) {
        br_ui_label_colored(ui, c->count ? "No lines match the filters." :
                            ed->console_capturing ? "Nothing yet. What the editor and your scripts print shows up here."
                                                  : "Nothing yet. Script print() shows up here.", ui->style.text_dim);
    } else {
        int hit;
        rows = br_ui_row(ui, CONSOLE_LINE_H * (float)n);
        br_ui_place(ui, rows);
        f = br_ui_area(ui, "##lines", 0);
        hit = (int)floorf((ui->mouse.y - rows.y) / CONSOLE_LINE_H);
        if (hit >= 0 && (uint32_t)hit < n && (f & (BR_UI_PRESSED | BR_UI_RIGHT_CLICKED | BR_UI_DOUBLE_CLICKED))) {
            ed->console_selected = (int)shown[hit];
            if (f & BR_UI_DOUBLE_CLICKED) open_line(ed, shown[hit]);
            if (f & BR_UI_RIGHT_CLICKED) ed_menu_console(ed, (int)shown[hit]);
        }
        {
            int from = (int)floorf((list.y - rows.y) / CONSOLE_LINE_H), to = (int)ceilf((list.y + list.h - rows.y) / CONSOLE_LINE_H);
            int k;
            if (from < 0) from = 0;
            if (to > (int)n) to = (int)n;
            for (k = from; k < to; ++k) {
                const ed_con_line *l = ed_console_line(c, shown[k]);
                float y = rows.y + (float)k * CONSOLE_LINE_H;
                br_color col = line_color(l);
                if ((int)shown[k] == ed->console_selected) br_fill_rect(ui->r, br_rect_make(rows.x - 2, y, rows.w + 4, CONSOLE_LINE_H), ui->style.selection);
                br_fill_rect(ui->r, br_rect_make(rows.x, y + 4, 4, 5), l->level == ED_LEVEL_LOG ? br_rgba(col.r, col.g, col.b, 110) : col);
                br_draw_text(ui->r, l->text, rows.x + 10, y + 3, 1, col);
                if (l->repeat > 1) {                               /* the same line again and again */
                    char times[16];
                    br_vec2 m;
                    snprintf(times, sizeof(times), "x%u", l->repeat);
                    m = br_measure_text(times, 1);
                    br_fill_rect(ui->r, br_rect_make(rows.x + rows.w - m.x - 8, y + 1, m.x + 6, CONSOLE_LINE_H - 2), ui->style.header);
                    br_draw_text(ui->r, times, rows.x + rows.w - m.x - 5, y + 3, 1, ui->style.text);
                }
            }
        }
    }
    if (br_ui_context_area(ui, list)) ed_menu_console(ed, -1);
    br_ui_panel_end(ui);
}
