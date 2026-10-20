/*
 * BRONTO EDITOR - commands: everything the user can do.
 *
 * Toolbar buttons, hierarchy buttons and keyboard shortcuts all call
 * these, so each action is written once. Each reports what happened on
 * the status bar.
 */
#include "editor.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void ed_status(editor *ed, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ed->status, sizeof(ed->status), fmt, ap);
    va_end(ap);
    ed->status_time = ed->engine ? ed->engine->time : 0;
}

static const char *file_name(const char *path) {
    const char *slash = strrchr(path, '/'), *back = strrchr(path, '\\');
    if (back && (!slash || back > slash)) slash = back;
    return slash ? slash + 1 : path;
}

static void stop_if_playing(editor *ed) {
    if (ed->doc.playing) ed_doc_stop(&ed->doc);
}

/* ---- files ------------------------------------------------------------------ */

static void do_new(editor *ed) {
    ed_doc_new(&ed->doc);
    ed->path_field[0] = 0;
    ed_status(ed, "New scene");
}

static void do_open(editor *ed) {
    if (!ed->path_field[0]) { ed_status(ed, "Type the scene's file name in the box, then Open"); return; }
    if (ed_doc_load(&ed->doc, ed->path_field)) {
        ed_status(ed, "Opened %s (%u entities)", file_name(ed->path_field), ed->doc.count);
        ed_cmd_frame(ed);
    } else {
        ed_status(ed, "Couldn't open %s: %s", file_name(ed->path_field), ed->doc.error);
    }
}

/* A scene saved below the project folder needs the project file, so a game
 * playing it (br_assets_find_root) resolves asset paths the same way. */
void ed_mark_project(editor *ed, const char *scene_path) {
    char dir[ED_PATH_MAX], marker[ED_PATH_MAX];
    const char *root = ed->assets.root;
    size_t n = strlen(root);
    br_path_parent(dir, sizeof(dir), scene_path);
    if (n && (root[n - 1] == '/' || root[n - 1] == '\\')) --n;
    if (strlen(dir) == n && strncmp(dir, root, n) == 0) return;      /* right in the project folder */
    br_path_join(marker, sizeof(marker), root, BR_PROJECT_FILE);
    if (!br_fs_exists(marker)) {
        const char text[] = "bronto_project 1\n# Asset paths in this project's scenes are relative to this folder.\n";
        br_file_write_all(marker, text, sizeof(text) - 1);
    }
}

static bool do_save(editor *ed) {
    const char *name;
    if (ed->doc.playing) { ed_status(ed, "Stop playing before saving"); return false; }
    if (!ed->path_field[0]) { ed_status(ed, "Type a file name (like level1.scene) in the box, then Save"); return false; }
    name = file_name(ed->path_field);
    if (!strchr(name, '.') && strlen(ed->path_field) + 6 < sizeof(ed->path_field))
        strcat(ed->path_field, ".scene");                       /* the usual extension */
    if (!ed_doc_save(&ed->doc, ed->path_field)) {
        ed_status(ed, "Couldn't save: %s", ed->doc.error);
        return false;
    }
    ed_mark_project(ed, ed->path_field);
    ed_status(ed, "Saved %s", file_name(ed->path_field));
    return true;
}

/* ---- projects ------------------------------------------------------------------ */

bool ed_open_project(editor *ed, const char *folder) {
    char abs[ED_PATH_MAX], marker[ED_PATH_MAX], main_scene[ED_PATH_MAX];
    if (!br_fs_absolute(folder, abs, sizeof(abs)) || !br_fs_is_dir(abs)) {
        ed_status(ed, "Can't open the project folder %s", folder);
        return false;
    }
    stop_if_playing(ed);
    ed_assets_destroy(ed);
    ed_assets_init(ed, abs);
    br_path_join(marker, sizeof(marker), abs, BR_PROJECT_FILE);
    if (!br_fs_exists(marker)) {                  /* mark it, so games resolve paths the same way */
        const char text[] = "bronto_project 1\n# Asset paths in this project's scenes are relative to this folder.\n";
        br_file_write_all(marker, text, sizeof(text) - 1);
    }
    br_path_join(main_scene, sizeof(main_scene), abs, "main.scene");
    if (br_fs_exists(main_scene) && ed_doc_load(&ed->doc, main_scene)) {
        snprintf(ed->path_field, sizeof(ed->path_field), "%s", main_scene);
        ed_cmd_frame(ed);
        ed_status(ed, "Opened project %s (main.scene)", br_path_name(abs));
    } else {
        ed_doc_new(&ed->doc);
        ed->path_field[0] = 0;
        ed_status(ed, "Opened project %s - double-click a scene to open it", br_path_name(abs));
    }
    return true;
}

void ed_choose_project(editor *ed, const char *folder) {
    stop_if_playing(ed);
    snprintf(ed->pending_project, sizeof(ed->pending_project), "%s", folder);
    if (ed_doc_dirty(&ed->doc)) ed->pending = ED_PENDING_PROJECT;
    else ed_open_project(ed, folder);
}

void ed_cmd_load_project(editor *ed) {
    char start[ED_PATH_MAX], chosen[ED_PATH_MAX];
    int r;
    if (!br_fs_absolute(ed->assets.root, start, sizeof(start))) start[0] = 0;
    r = br_pick_folder(ed->engine, "Choose the project folder", start, chosen, sizeof(chosen));
    if (r == 1) ed_choose_project(ed, chosen);
    else if (r == 0) ed_status(ed, "Load Project cancelled");
    else {                                        /* no OS dialog here: the editor's own */
        ed->picker.open = true;
        snprintf(ed->picker.dir, sizeof(ed->picker.dir), "%s", start[0] ? start : "/");
        br_dir_list_free(&ed->picker.list);
        br_fs_list(ed->picker.dir, false, &ed->picker.list);
    }
}

void ed_cmd_new(editor *ed) {
    stop_if_playing(ed);
    if (ed_doc_dirty(&ed->doc)) ed->pending = ED_PENDING_NEW;
    else do_new(ed);
}

void ed_cmd_open(editor *ed) {
    stop_if_playing(ed);
    if (ed_doc_dirty(&ed->doc)) ed->pending = ED_PENDING_OPEN;
    else do_open(ed);
}

void ed_cmd_save(editor *ed) { do_save(ed); }

bool ed_cmd_quit(editor *ed) {
    stop_if_playing(ed);
    if (ed_doc_dirty(&ed->doc) && !ed->headless) {
        ed->pending = ED_PENDING_QUIT;
        return false;
    }
    br_engine_quit(ed->engine);
    return true;
}

/* The unsaved-changes dialog: 0 = save first, 1 = throw the changes away, 2 = cancel. */
void ed_cmd_answer(editor *ed, int choice) {
    ed_pending what = ed->pending;
    if (choice == 2 || what == ED_PENDING_NONE) { ed->pending = ED_PENDING_NONE; return; }
    if (choice == 0) {
        char wanted[ED_PATH_MAX];
        snprintf(wanted, sizeof(wanted), "%s", ed->path_field);   /* Open names a new file: save the old one */
        if (what == ED_PENDING_OPEN || what == ED_PENDING_PROJECT) snprintf(ed->path_field, sizeof(ed->path_field), "%s", ed->doc.path);
        if (!do_save(ed)) { snprintf(ed->path_field, sizeof(ed->path_field), "%s", wanted); ed->pending = ED_PENDING_NONE; return; }
        snprintf(ed->path_field, sizeof(ed->path_field), "%s", wanted);
    }
    ed->pending = ED_PENDING_NONE;
    if (what == ED_PENDING_NEW) do_new(ed);
    else if (what == ED_PENDING_OPEN) do_open(ed);
    else if (what == ED_PENDING_QUIT) br_engine_quit(ed->engine);
    else if (what == ED_PENDING_PROJECT) ed_open_project(ed, ed->pending_project);
}

/* ---- history and play mode ------------------------------------------------------- */

void ed_cmd_undo(editor *ed) {
    if (ed->doc.playing) { ed_status(ed, "Stop playing to undo"); return; }
    ed_doc_checkpoint(&ed->doc);
    ed_status(ed, ed_doc_undo(&ed->doc) ? "Undo" : "Nothing to undo");
}

void ed_cmd_redo(editor *ed) {
    if (ed->doc.playing) return;
    ed_status(ed, ed_doc_redo(&ed->doc) ? "Redo" : "Nothing to redo");
}

void ed_cmd_play(editor *ed) {
    ed->script_errors_seen = br_script_error_count(&ed->doc.world);
    if (ed->doc.playing) {
        ed_doc_stop(&ed->doc);
        ed_status(ed, "Stopped: the scene is back as it was");
    } else {
        ed_doc_play(&ed->doc);
        ed_status(ed, "Playing. Changes made now are undone by Stop (F5)");
    }
}

/* ---- entities ----------------------------------------------------------------------- */

static br_transform *selected_transform(editor *ed) {
    return (br_transform *)br_get(&ed->doc.world, ed->doc.selected, ed->doc.ids.transform);
}

void ed_cmd_add_entity(editor *ed) {
    br_vec2 at = ed->view.center;
    br_entity e;
    if (ed->view.snap) at = br_v2(roundf(at.x / ed->view.grid_size) * ed->view.grid_size, roundf(at.y / ed->view.grid_size) * ed->view.grid_size);
    e = ed_doc_create(&ed->doc, "Entity", at);
    br_add(&ed->doc.world, e, ed->doc.ids.shape, NULL);          /* something to see and grab */
    ed_status(ed, "Added an entity. Add components in the inspector");
}

void ed_cmd_duplicate(editor *ed) {
    char name[64];
    if (ed->doc.selected == BR_ENTITY_NULL) return;
    if (ed_doc_duplicate(&ed->doc, ed->doc.selected) != BR_ENTITY_NULL)
        ed_status(ed, "Duplicated as %s", ed_doc_name(&ed->doc, ed->doc.selected, name, sizeof(name)));
}

void ed_cmd_delete(editor *ed) {
    char name[64];
    if (ed->doc.selected == BR_ENTITY_NULL) return;
    ed_doc_name(&ed->doc, ed->doc.selected, name, sizeof(name));
    ed_doc_delete(&ed->doc, ed->doc.selected);
    ed_status(ed, "Deleted %s (Ctrl+Z brings it back)", name);
}


void ed_cmd_nudge(editor *ed, br_vec2 delta) {
    br_transform *t = selected_transform(ed);
    if (t) t->position = br_v2_add(t->position, delta);
}

void ed_cmd_frame(editor *ed) {
    br_transform *t = selected_transform(ed);
    ed->view.center = t ? t->position : br_v2_scale(ed->view.game_size, 0.5f);
}

/* ---- the scene a fresh editor opens with ----------------------------------------------- */

static br_entity add(ed_doc *d, const char *name, float x, float y) { return ed_doc_create(d, name, br_v2(x, y)); }

static void shape(ed_doc *d, br_entity e, int kind, float w, float h, br_color color, br_color outline, int layer) {
    br_shape *s = (br_shape *)br_add(&d->world, e, d->ids.shape, NULL);
    s->kind = kind;
    s->size = br_v2(w, h);
    s->color = color;
    s->outline = outline;
    s->layer = layer;
}

static void label(ed_doc *d, br_entity e, const char *text, float size, br_color color) {
    br_label *l = (br_label *)br_add(&d->world, e, d->ids.label, NULL);
    snprintf(l->text, sizeof(l->text), "%s", text);
    l->size = size;
    l->color = color;
}

static void motion(ed_doc *d, br_entity e, float vx, float vy, float spin) {
    br_motion *m = (br_motion *)br_add(&d->world, e, d->ids.motion, NULL);
    m->velocity = br_v2(vx, vy);
    m->spin = spin;
}

static void bounds(ed_doc *d, br_entity e, float x0, float y0, float x1, float y1, bool wrap) {
    br_bounds *b = (br_bounds *)br_add(&d->world, e, d->ids.bounds, NULL);
    b->min = br_v2(x0, y0);
    b->max = br_v2(x1, y1);
    b->wrap = wrap;
}

void ed_sample_scene(ed_doc *d) {
    br_entity e;
    ed_doc_new(d);
    e = add(d, "Sky", 160, 90);     shape(d, e, BR_SHAPE_RECT, 320, 180, br_rgb(28, 40, 70), br_rgba(0, 0, 0, 0), -10);
    e = add(d, "Ground", 160, 172); shape(d, e, BR_SHAPE_RECT, 320, 16, br_rgb(70, 140, 70), br_rgba(0, 0, 0, 0), -5);
    e = add(d, "Title", 160, 28);   label(d, e, "Hello, Bronto!", 2, br_rgb(255, 220, 90));
    e = add(d, "Ball", 60, 100);    shape(d, e, BR_SHAPE_ELLIPSE, 20, 20, br_rgb(255, 140, 50), br_rgb(255, 230, 200), 0);
    motion(d, e, 70, -55, 0);       bounds(d, e, 10, 10, 310, 154, false);
    e = add(d, "Spinner", 250, 96); shape(d, e, BR_SHAPE_RECT, 26, 26, br_rgb(170, 90, 230), br_rgb(255, 255, 255), 0);
    motion(d, e, 0, 0, 90);
    e = add(d, "Arrow", 150, 120);  shape(d, e, BR_SHAPE_TRIANGLE, 14, 18, br_rgb(240, 240, 240), br_rgba(0, 0, 0, 0), 1);
    ((br_transform *)br_get(&d->world, e, d->ids.transform))->rotation = 90;
    motion(d, e, 50, 0, 0);         bounds(d, e, 0, 0, 320, 180, true);
    e = add(d, "Hint", 160, 148);   label(d, e, "Press Play (F5) to run me", 1, br_rgb(200, 210, 230));
    d->selected = BR_ENTITY_NULL;
    ed_doc_mark_clean(d);
}
