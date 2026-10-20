/*
 * BRONTO EDITOR - the panels around the scene view.
 *
 *   toolbar     New / Open / Save, the file name, Undo / Redo, Play, Grid / Snap
 *   hierarchy   every entity in the scene, in order
 *   inspector   the selected entity's components, generated from their
 *               descriptions, so any described component is editable
 *   status bar  what just happened, and a few numbers
 *   dialog      "save your changes?" before New, Open or Quit
 */
#include "editor.h"
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

/* While a hierarchy row is dragged: a line where it would land, and the move on release. */
static void reorder_drop(editor *ed, br_rect panel, const br_rect *rows, uint32_t n) {
    br_ui *ui = &ed->ui;
    const char *payload = br_ui_dragging(ui, ED_ENTITY_DRAG), *dropped;
    uint32_t slot = 0;
    float y;
    if (!payload || n == 0) return;
    while (slot < n && ui->mouse.y > rows[slot].y + rows[slot].h / 2) ++slot;
    y = slot < n ? rows[slot].y - 2 : rows[n - 1].y + rows[n - 1].h + 1;
    if (ui->mouse.x >= panel.x && ui->mouse.x < panel.x + panel.w && ui->mouse.y >= panel.y && ui->mouse.y < panel.y + panel.h)
        br_fill_rect(ui->r, br_rect_make(rows[0].x, floorf(y), rows[0].w, 2), ui->style.accent);
    if ((dropped = br_ui_drop_target(ui, panel, ED_ENTITY_DRAG)) != NULL) {
        br_entity e = (br_entity)strtoul(dropped, NULL, 10);
        ed_doc_move(&ed->doc, e, (int)slot);
        ed->doc.selected = e;
    }
}

void ed_hierarchy(editor *ed, br_rect rect) {
    br_ui *ui = &ed->ui;
    ed_doc *d = &ed->doc;
    uint32_t i;
    char name[64];
    static br_rect rows[ED_LIST_ROWS];
    br_ui_panel_begin(ui, "hierarchy", rect);
    br_ui_heading(ui, "Scene");
    br_ui_columns(ui, 3);
    if (br_ui_button(ui, "+ New")) ed_cmd_add_entity(ed);
    if (br_ui_button(ui, "Copy")) ed_cmd_duplicate(ed);
    if (br_ui_button(ui, "Delete")) ed_cmd_delete(ed);
    br_ui_separator(ui);
    for (i = 0; i < d->count; ++i) {                     /* drag a row up or down to reorder */
        br_entity e = d->order[i];
        char handle[16];
        br_ui_push_id_int(ui, (int)e);
        if (br_ui_selectable(ui, ed_doc_name(d, e, name, sizeof(name)), e == d->selected)) d->selected = e;
        snprintf(handle, sizeof(handle), "%u", (unsigned)e);
        br_ui_drag_source(ui, ED_ENTITY_DRAG, handle, name);
        br_ui_pop_id(ui);
        rows[i < ED_LIST_ROWS ? i : ED_LIST_ROWS - 1] = br_ui_last_rect(ui);
    }
    reorder_drop(ed, rect, rows, d->count < ED_LIST_ROWS ? d->count : ED_LIST_ROWS);
    if (d->count == 0) {
        br_ui_label_colored(ui, "The scene is empty.", ui->style.text_dim);
        br_ui_label_colored(ui, "Press + New to add", ui->style.text_dim);
        br_ui_label_colored(ui, "an entity.", ui->style.text_dim);
    }
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
static void edit_field(br_ui *ui, const br_component_desc *desc, const br_field *f, void *component) {
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
        if (!desc || br_has(w, e, c)) continue;
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
    if ((name = (br_name *)br_get(w, e, d->ids.name)) != NULL)
        br_ui_text_field(ui, "Name", name->text, sizeof(name->text));
    br_ui_spacing(ui, 4);
    for (c = 0; c < br_component_type_count(w); ++c) {
        const br_component_desc *desc = br_component_get_desc(w, c);
        bool open, remove = false;
        int i;
        void *data;
        if (!desc || c == d->ids.name || !(data = br_get(w, e, c))) continue;
        open = !ed->section_closed[c];
        br_ui_push_id(ui, desc->name);
        if (br_ui_header(ui, desc->name, &open, &remove)) {
            br_ui_indent(ui, 6);
            for (i = 0; i < desc->field_count; ++i) edit_field(ui, desc, &desc->fields[i], br_get(w, e, c));
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
    br_ui_panel_end(ui);
}

/* ---- status bar ------------------------------------------------------------------------ */

void ed_statusbar(editor *ed, br_rect rect) {
    br_ui *ui = &ed->ui;
    char info[ED_PATH_MAX + 160], file[ED_PATH_MAX + 8];
    bool fresh = ed->engine->time - ed->status_time < 6.0 && ed->status[0];
    br_vec2 size;
    br_ui_panel_begin(ui, "status", rect);
    br_draw_text(ui->r, fresh ? ed->status : "Wheel: zoom   Right-drag: pan   F5: play   F: frame selection",
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
