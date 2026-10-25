/*
 * BRONTO EDITOR - right-click menus.
 *
 *   hierarchy row / scene object    rename, add child, duplicate, cut / copy / paste,
 *                                   add component, unparent, frame, prefab, delete
 *   hierarchy / scene empty space   new entity (here), paste (here), fold, view options
 *   asset / folder empty space      open, place, play, edit, rename, delete, show in
 *                                   the file manager / new folder, new script, refresh
 *   inspector section or field      reset, copy / paste values, edit script, remove
 *   inspector empty space           add component, paste component
 *   console line                    open the script at that line, clear, filters
 *
 * Panels call ed_menu_*() when something is right-clicked: that remembers
 * what it was (ed->menu) and opens the menu; ed_menus() lists the open
 * menu's items each frame and runs the chosen one. The actions are the
 * same commands the buttons and shortcuts use.
 */
#include "editor.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- opening ------------------------------------------------------------------------ */

void ed_menu_entity(editor *ed, br_entity e) {
    if (!br_entity_alive(&ed->doc.world, e)) return;
    ed_clipboard_refresh(ed);                             /* entities copied in another editor paste too */
    ed->doc.selected = e;                                 /* right-click selects, like a click */
    ed->menu.entity = e;
    br_ui_menu_open(&ed->ui, "entity");
}

void ed_menu_hierarchy(editor *ed) { ed_clipboard_refresh(ed); br_ui_menu_open(&ed->ui, "hierarchy"); }

void ed_menu_view(editor *ed, br_vec2 world) {
    br_entity e = ed_doc_pick(&ed->doc, world);
    ed->menu.world = world;
    if (e != BR_ENTITY_NULL) ed_menu_entity(ed, e);
    else { ed_clipboard_refresh(ed); br_ui_menu_open(&ed->ui, "view"); }
}

void ed_menu_asset(editor *ed, const char *rel) {
    snprintf(ed->menu.asset, sizeof(ed->menu.asset), "%s", rel);
    if (rel[0]) ed_assets_select(ed, rel);
    br_ui_menu_open(&ed->ui, rel[0] ? "asset" : "folder");
}

void ed_menu_component(editor *ed, br_component c, int field) {
    ed->menu.entity = ed->doc.selected;
    ed->menu.component = c;
    ed->menu.field = field;
    br_ui_menu_open(&ed->ui, "component");
}

void ed_menu_inspector(editor *ed) {
    ed->menu.entity = ed->doc.selected;
    br_ui_menu_open(&ed->ui, "inspector");
}

void ed_menu_console(editor *ed, int line) {
    ed->menu.line = line;
    ed->console_selected = line;
    br_ui_menu_open(&ed->ui, "console");
}

/* ---- copying text to the system clipboard ------------------------------------------------ */

void ed_copy_text(editor *ed, const char *text, const char *what) {
    if (br_clipboard_set(ed->engine, text)) ed_status(ed, "Copied %s", what);
    else ed_status(ed, "Couldn't copy to the clipboard");
}

/* One console line, or (line -1) every line the filters show, one per line. */
void ed_console_copy(editor *ed, int line) {
    ed_console *c = &ed->console;
    size_t cap = 1, used = 0;
    uint32_t i;
    char *text;
    for (i = 0; i < c->count; ++i) cap += strlen(ed_console_line(c, i)->text) + 16;
    if (!(text = (char *)br_alloc(cap))) return;
    text[0] = 0;
    for (i = 0; i < c->count; ++i) {
        const ed_con_line *l = ed_console_line(c, i);
        if (line >= 0 ? (int)i != line : !ed->console_show[l->level]) continue;
        used += (size_t)snprintf(text + used, cap - used, l->repeat > 1 ? "%s (x%u)\n" : "%s\n", l->text, l->repeat);
    }
    if (used > 0 && line >= 0) text[used - 1] = 0;        /* one line: no newline after it */
    ed_copy_text(ed, text, line >= 0 ? "the line" : "the console");
    br_free(text);
}

static void copy_path(editor *ed, const char *rel, bool full) {
    char path[ED_PATH_MAX], abs[ED_PATH_MAX];
    if (!full) { ed_copy_text(ed, rel, rel[0] ? rel : "the path"); return; }
    ed_asset_path(ed, rel, path, sizeof(path));
    if (!br_fs_absolute(path[0] ? path : ".", abs, sizeof(abs))) snprintf(abs, sizeof(abs), "%s", path);
    ed_copy_text(ed, abs, abs);
}

/* ---- opening a script named in a message ----------------------------------------------- */

/* Looks for `name` under `dir` (relative to the project), a few folders deep. */
static bool find_file(editor *ed, const char *dir, const char *name, int depth, char *out, size_t size) {
    char full[ED_PATH_MAX], rel[ED_PATH_MAX];
    br_dir_list l;
    int i;
    bool found = false;
    ed_asset_path(ed, dir, full, sizeof(full));
    if (!br_fs_list(full[0] ? full : ".", false, &l)) return false;
    for (i = 0; i < l.count && !found; ++i) {
        const br_dir_entry *e = &l.entries[i];
        if (dir[0]) snprintf(rel, sizeof(rel), "%s/%s", dir, e->name);
        else snprintf(rel, sizeof(rel), "%s", e->name);
        if (!e->is_dir && strcmp(e->name, name) == 0) { snprintf(out, size, "%s", rel); found = true; }
    }
    for (i = 0; i < l.count && !found && depth > 0; ++i) {
        const br_dir_entry *e = &l.entries[i];
        if (!e->is_dir || strcmp(e->name, "build") == 0) continue;
        if (dir[0]) snprintf(rel, sizeof(rel), "%s/%s", dir, e->name);
        else snprintf(rel, sizeof(rel), "%s", e->name);
        found = find_file(ed, rel, name, depth - 1, out, size);
    }
    br_dir_list_free(&l);
    return found;
}

/* Messages name scripts by file name ("player.bs:12"): the scene's own
 * Script components say which file that is, else the project is searched. */
bool ed_open_script_named(editor *ed, const char *name, int line) {
    br_world *w = &ed->doc.world;
    br_component sc = br_component_find(w, "Script");
    char full[ED_PATH_MAX], rel[ED_PATH_MAX];
    uint32_t i;
    ed_asset_path(ed, name, full, sizeof(full));
    if (br_fs_exists(full)) return ed_open_script(ed, name, line);
    for (i = 0; sc != BR_COMPONENT_INVALID && i < ed->doc.count; ++i) {
        const br_script *s = (const br_script *)br_get(w, ed->doc.order[i], sc);
        if (s && strcmp(br_path_name(s->file), br_path_name(name)) == 0) return ed_open_script(ed, s->file, line);
    }
    if (find_file(ed, "", br_path_name(name), 6, rel, sizeof(rel))) return ed_open_script(ed, rel, line);
    ed_status(ed, "Couldn't find %s in the project", name);
    return false;
}

/* ---- components: reset, copy, paste ---------------------------------------------------- */

void ed_component_reset(editor *ed, br_entity e, br_component c, int field) {
    br_world *w = &ed->doc.world;
    const br_component_desc *desc = br_component_get_desc(w, c);
    unsigned char *data = (unsigned char *)br_get(w, e, c);
    int i;
    if (!desc || !data) return;
    for (i = 0; i < desc->field_count; ++i) {                 /* field by field: anything else stays */
        const br_field *f = &desc->fields[i];
        if (field >= 0 && i != field) continue;
        if (desc->defaults) memcpy(data + f->offset, (const unsigned char *)desc->defaults + f->offset, f->size);
        else memset(data + f->offset, 0, f->size);
    }
}

bool ed_component_copy(editor *ed, br_entity e, br_component c) {
    const br_component_desc *desc = br_component_get_desc(&ed->doc.world, c);
    const void *data = br_get(&ed->doc.world, e, c);
    if (!desc || !data || desc->size > sizeof(ed->component_clip.data)) return false;
    snprintf(ed->component_clip.name, sizeof(ed->component_clip.name), "%s", desc->name);
    memcpy(ed->component_clip.data, data, desc->size);
    ed->component_clip.full = true;
    return true;
}

bool ed_component_paste(editor *ed, br_entity e, br_component c) {
    br_world *w = &ed->doc.world;
    const br_component_desc *desc;
    unsigned char *data;
    int i;
    if (!ed->component_clip.full) return false;
    if (c == BR_COMPONENT_INVALID) c = br_component_find(w, ed->component_clip.name);
    desc = br_component_get_desc(w, c);
    if (!desc || strcmp(desc->name, ed->component_clip.name) != 0) return false;
    if (!(data = (unsigned char *)br_get(w, e, c)) && !(data = (unsigned char *)br_add(w, e, c, NULL))) return false;
    for (i = 0; i < desc->field_count; ++i) {                 /* described fields only: never runtime state */
        const br_field *f = &desc->fields[i];
        memcpy(data + f->offset, ed->component_clip.data + f->offset, f->size);
    }
    return true;
}

/* ---- the menus ------------------------------------------------------------------------------ */

static bool item(editor *ed, const char *label, const char *keys, bool enabled) { return br_ui_menu_item(&ed->ui, label, keys, enabled); }
static void sep(editor *ed) { br_ui_menu_separator(&ed->ui); }

/* "Add Component": a submenu of what the entity doesn't have yet. */
static void add_component_items(editor *ed, br_entity e) {
    br_world *w = &ed->doc.world;
    uint32_t c;
    if (!br_ui_menu_sub_begin(&ed->ui, "Add Component", !ed->doc.playing && br_entity_alive(w, e))) return;
    for (c = 0; c < br_component_type_count(w); ++c) {
        const br_component_desc *desc = br_component_get_desc(w, c);
        if (!desc || br_has(w, e, c) || strcmp(desc->name, "Parent") == 0) continue;
        if (item(ed, desc->name, NULL, true)) {
            br_add(w, e, c, NULL);
            ed->section_closed[c] = false;
            ed->doc.selected = e;
            ed_status(ed, "Added %s", desc->name);
        }
    }
    br_ui_menu_sub_end(&ed->ui);
}

static br_entity new_entity_at(editor *ed, const char *name, br_vec2 at, bool shape) {
    br_entity e;
    if (ed->view.snap) at = br_v2(roundf(at.x / ed->view.grid_size) * ed->view.grid_size, roundf(at.y / ed->view.grid_size) * ed->view.grid_size);
    e = ed_doc_create(&ed->doc, name, at);
    if (e != BR_ENTITY_NULL && shape) br_add(&ed->doc.world, e, ed->doc.ids.shape, NULL);
    return e;
}

static void entity_menu(editor *ed) {
    ed_doc *d = &ed->doc;
    br_world *w = &d->world;
    br_entity e = ed->menu.entity, parent;
    bool edit = !d->playing, kids;
    if (!br_ui_menu_begin(&ed->ui, "entity")) return;
    if (!br_entity_alive(w, e)) { br_ui_menu_close(&ed->ui); return; }
    parent = br_parent_of(w, e);
    kids = br_children(w, e, NULL, 0) > 0;
    if (item(ed, "Rename", "F2", true)) { d->selected = e; ed_cmd_rename(ed); }
    if (item(ed, "Add Child", NULL, edit)) { d->selected = e; ed_cmd_add_child(ed); }
    if (item(ed, "Duplicate", "Ctrl+D", edit)) { d->selected = e; ed_cmd_duplicate(ed); }
    sep(ed);
    if (item(ed, "Cut", "Ctrl+X", edit)) { d->selected = e; ed_cmd_cut(ed); }
    if (item(ed, "Copy", "Ctrl+C", true)) { d->selected = e; ed_cmd_copy(ed); }
    if (item(ed, "Paste", "Ctrl+V", edit && ed->clipboard)) ed_cmd_paste(ed, NULL, parent);
    if (item(ed, "Paste as Child", NULL, edit && ed->clipboard)) ed_cmd_paste(ed, NULL, e);
    sep(ed);
    add_component_items(ed, e);
    if (item(ed, "Unparent", NULL, edit && parent != BR_ENTITY_NULL)) { d->selected = e; ed_cmd_unparent(ed); }
    if (kids && item(ed, ed_collapsed(ed, e) ? "Expand" : "Collapse", NULL, true)) ed_set_collapsed(ed, e, !ed_collapsed(ed, e));
    if (item(ed, "Frame in View", "F", true)) { d->selected = e; ed_cmd_frame(ed); }
    if (item(ed, "Save as Prefab", NULL, edit)) { d->selected = e; ed_assets_save_prefab(ed); }
    sep(ed);
    if (item(ed, "Delete", "Del", edit)) { d->selected = e; ed_cmd_delete(ed); }
    br_ui_menu_end(&ed->ui);
}

static void hierarchy_menu(editor *ed) {
    ed_doc *d = &ed->doc;
    bool edit = !d->playing;
    uint32_t i;
    if (!br_ui_menu_begin(&ed->ui, "hierarchy")) return;
    if (item(ed, "New Entity", NULL, edit)) { d->selected = BR_ENTITY_NULL; ed_cmd_add_entity(ed); }
    if (item(ed, "Paste", "Ctrl+V", edit && ed->clipboard)) ed_cmd_paste(ed, NULL, BR_ENTITY_NULL);
    sep(ed);
    if (item(ed, "Expand All", NULL, ed->collapsed_count > 0)) ed->collapsed_count = 0;
    if (item(ed, "Collapse All", NULL, d->count > 0))
        for (i = 0; i < d->count; ++i)
            if (br_children(&d->world, d->order[i], NULL, 0) > 0) ed_set_collapsed(ed, d->order[i], true);
    br_ui_menu_end(&ed->ui);
}

static void view_menu(editor *ed) {
    ed_doc *d = &ed->doc;
    bool edit = !d->playing;
    br_vec2 at = ed->menu.world;
    if (!br_ui_menu_begin(&ed->ui, "view")) return;
    if (item(ed, "New Entity Here", NULL, edit)) { new_entity_at(ed, "Entity", at, true); ed_status(ed, "Added an entity"); }
    if (item(ed, "New Label Here", NULL, edit)) {
        br_entity e = new_entity_at(ed, "Label", at, false);
        if (e != BR_ENTITY_NULL) snprintf(((br_label *)br_add(&d->world, e, d->ids.label, NULL))->text, sizeof(((br_label *)0)->text), "Text");
        ed_status(ed, "Added a label");
    }
    if (item(ed, "New Camera Here", NULL, edit)) {      /* the game shows what it sees (components.h) */
        br_entity e = new_entity_at(ed, "Camera", at, false);
        if (e != BR_ENTITY_NULL) {
            br_camera *c = (br_camera *)br_add(&d->world, e, d->ids.camera, NULL);
            c->limit_max = ed->view.game_size;                  /* limits a screen's worth, to start with */
            c->current = br_camera_current(&d->world) == e;     /* the first is the one used */
        }
        ed_status(ed, "Added a camera: set Camera.follow to the player, and its limits to the level");
    }
    if (item(ed, "New Animated Sprite Here", NULL, edit)) {
        br_entity e = new_entity_at(ed, "Sprite", at, false);
        if (e != BR_ENTITY_NULL) {
            br_sprite *sp = (br_sprite *)br_add(&d->world, e, d->ids.sprite, NULL);
            sp->frame_size = br_v2(16, 16);
            br_add(&d->world, e, d->ids.animation, NULL);
        }
        ed_status(ed, "Added an animated sprite: drop a sprite sheet on Sprite.image, set frame_size and Animation.frames");
    }
    if (item(ed, "Paste Here", "Ctrl+V", edit && ed->clipboard)) ed_cmd_paste(ed, &at, BR_ENTITY_NULL);
    sep(ed);
    if (item(ed, "Frame Selection", "F", br_entity_alive(&d->world, d->selected))) ed_cmd_frame(ed);
    if (item(ed, "Reset View", NULL, true)) { ed->view.zoom = 2.0f; ed->view.center = br_v2_scale(ed->view.game_size, 0.5f); }
    if (br_ui_menu_check(&ed->ui, "Grid", ed->view.grid)) ed->view.grid = !ed->view.grid;
    if (br_ui_menu_check(&ed->ui, "Snap to Grid", ed->view.snap)) ed->view.snap = !ed->view.snap;
    if (br_ui_menu_check(&ed->ui, "Show Colliders", ed->show_colliders)) ed->show_colliders = !ed->show_colliders;
    if (br_ui_menu_sub_begin(&ed->ui, "Panels", true)) {      /* (drag their edges to resize them) */
        int i;
        for (i = 0; i < ED_PANEL_COUNT; ++i)
            if (br_ui_menu_check(&ed->ui, ed_panel_name((ed_panel)i), ed->panels.open[i])) ed_panel_set_open(ed, (ed_panel)i, !ed->panels.open[i]);
        sep(ed);
        if (item(ed, "Reset Layout", NULL, true)) {
            ed_panels_default(&ed->panels);
            ed_prefs_save(ed);
            ed_status(ed, "The panels are back to their first sizes");
        }
        br_ui_menu_sub_end(&ed->ui);
    }
    sep(ed);
    if (item(ed, d->playing ? "Stop" : "Play", "F5", true)) ed_cmd_play(ed);
    br_ui_menu_end(&ed->ui);
}

static void show_in_file_manager(editor *ed, const char *rel, bool is_dir) {
    char full[ED_PATH_MAX], dir[ED_PATH_MAX], abs[ED_PATH_MAX];
    ed_asset_path(ed, rel, full, sizeof(full));
    if (is_dir) snprintf(dir, sizeof(dir), "%s", full);
    else br_path_parent(dir, sizeof(dir), full);
    if (!dir[0]) snprintf(dir, sizeof(dir), ".");
    if (br_fs_absolute(dir, abs, sizeof(abs))) snprintf(dir, sizeof(dir), "%s", abs);
    if (!br_open_path(ed->engine, dir)) ed_status(ed, "Couldn't open %s", dir);
}

static void asset_menu(editor *ed) {
    const char *rel = ed->menu.asset;
    char full[ED_PATH_MAX];
    ed_asset_kind k;
    bool edit = !ed->doc.playing, is_dir;
    if (!br_ui_menu_begin(&ed->ui, "asset")) return;
    ed_asset_path(ed, rel, full, sizeof(full));
    is_dir = br_fs_is_dir(full);
    k = ed_asset_kind_of(rel, is_dir);
    switch (k) {
    case ED_ASSET_FOLDER:
        if (item(ed, "Open", NULL, true)) ed_assets_open_dir(ed, rel);
        break;
    case ED_ASSET_SCENE:
        if (item(ed, "Open Scene", NULL, true)) ed_assets_activate(ed, rel);
        if (item(ed, "Place in Scene", NULL, edit)) ed_assets_drop(ed, rel, ed->view.center);
        break;
    case ED_ASSET_IMAGE:
        if (item(ed, "Add to Scene", NULL, edit)) ed_assets_drop(ed, rel, ed->view.center);
        break;
    case ED_ASSET_SOUND: {
        bool playing = ed->assets.voice && br_voice_playing(&ed->engine->mixer, ed->assets.voice);
        if (item(ed, playing ? "Stop" : "Play", NULL, true)) ed_assets_play(ed, rel);
        if (item(ed, "Add to Scene", NULL, edit)) ed_assets_drop(ed, rel, ed->view.center);
        break;
    }
    case ED_ASSET_SCRIPT: {
        bool target = br_entity_alive(&ed->doc.world, ed->doc.selected);
        if (item(ed, "Edit Script", NULL, true)) ed_assets_edit(ed, rel);
        if (item(ed, "Attach to Selected", NULL, edit && target))
            ed_assets_drop(ed, rel, br_world_transform(&ed->doc.world, ed->doc.selected).position);
        break;
    }
    default:
        break;
    }
    if (!is_dir && item(ed, "Open with System App", NULL, true) && !br_open_path(ed->engine, full)) ed_status(ed, "Couldn't open %s", rel);
    sep(ed);
    if (item(ed, "Rename", "F2", true)) { ed_assets_select(ed, rel); ed->assets.focus_name = true; }
    if (item(ed, "Delete", NULL, true)) { ed_assets_select(ed, rel); ed_assets_delete(ed); }
    sep(ed);
    if (item(ed, "Copy Path", NULL, true)) copy_path(ed, rel, false);
    if (item(ed, "Copy Full Path", NULL, true)) copy_path(ed, rel, true);
    if (item(ed, "Show in File Manager", NULL, true)) show_in_file_manager(ed, rel, false);
    br_ui_menu_end(&ed->ui);
}

static void folder_menu(editor *ed) {
    bool edit = !ed->doc.playing;
    if (!br_ui_menu_begin(&ed->ui, "folder")) return;
    if (item(ed, "New Folder", NULL, true)) ed_assets_new_folder(ed);
    if (item(ed, "New Script", NULL, true)) ed_assets_new_script(ed);
    if (item(ed, "Save Selected as Prefab", NULL, edit && br_entity_alive(&ed->doc.world, ed->doc.selected))) ed_assets_save_prefab(ed);
    sep(ed);
    if (item(ed, "Up", NULL, ed->assets.dir[0] != 0)) {
        char up[ED_PATH_MAX];
        br_path_parent(up, sizeof(up), ed->assets.dir);
        ed_assets_open_dir(ed, up);
    }
    if (item(ed, "Refresh", NULL, true)) { ed_assets_refresh(ed); ed_status(ed, "Reloaded the folder and every asset"); }
    if (item(ed, "Copy Folder Path", NULL, true)) copy_path(ed, ed->assets.dir, true);
    if (item(ed, "Show in File Manager", NULL, true)) show_in_file_manager(ed, ed->assets.dir, true);
    br_ui_menu_end(&ed->ui);
}

static void component_menu(editor *ed) {
    br_world *w = &ed->doc.world;
    br_entity e = ed->menu.entity;
    br_component c = ed->menu.component;
    const br_component_desc *desc = br_component_get_desc(w, c);
    bool edit = true;                                     /* (values can be tried out while playing too) */
    char label[96];
    if (!br_ui_menu_begin(&ed->ui, "component")) return;
    if (!desc || !br_get(w, e, c)) { br_ui_menu_close(&ed->ui); return; }
    if (ed->menu.field >= 0 && ed->menu.field < desc->field_count) {
        snprintf(label, sizeof(label), "Reset %s", desc->fields[ed->menu.field].name);
        if (item(ed, label, NULL, edit)) ed_component_reset(ed, e, c, ed->menu.field);
        sep(ed);
    }
    snprintf(label, sizeof(label), "Reset %s", desc->name);
    if (item(ed, label, NULL, edit)) { ed_component_reset(ed, e, c, -1); ed_status(ed, "Reset %s", desc->name); }
    if (item(ed, "Copy Values", NULL, true) && ed_component_copy(ed, e, c)) ed_status(ed, "Copied %s", desc->name);
    if (item(ed, "Paste Values", NULL, edit && ed->component_clip.full && strcmp(ed->component_clip.name, desc->name) == 0))
        ed_component_paste(ed, e, c);
    if (strcmp(desc->name, "Script") == 0) {
        const br_script *s = (const br_script *)br_get(w, e, c);
        sep(ed);
        if (item(ed, "Edit Script", NULL, s && s->file[0])) ed_open_script(ed, s->file, 1);
    }
    sep(ed);
    snprintf(label, sizeof(label), "Remove %s", desc->name);
    if (item(ed, label, NULL, edit)) { br_remove(w, e, c); ed_status(ed, "Removed %s", desc->name); }
    br_ui_menu_end(&ed->ui);
}

static void inspector_menu(editor *ed) {
    br_world *w = &ed->doc.world;
    br_entity e = ed->menu.entity;
    br_component clip = ed->component_clip.full ? br_component_find(w, ed->component_clip.name) : BR_COMPONENT_INVALID;
    char label[64];
    if (!br_ui_menu_begin(&ed->ui, "inspector")) return;
    if (!br_entity_alive(w, e)) { br_ui_menu_close(&ed->ui); return; }
    add_component_items(ed, e);
    snprintf(label, sizeof(label), "Paste %s", ed->component_clip.full ? ed->component_clip.name : "Component");
    if (item(ed, label, NULL, clip != BR_COMPONENT_INVALID && !br_has(w, e, clip)) && ed_component_paste(ed, e, BR_COMPONENT_INVALID))
        ed_status(ed, "Pasted %s", ed->component_clip.name);
    sep(ed);
    if (item(ed, "Rename", "F2", true)) ed_cmd_rename(ed);
    br_ui_menu_end(&ed->ui);
}

static void console_menu(editor *ed) {
    const ed_con_line *l = ed->menu.line >= 0 ? ed_console_line(&ed->console, (uint32_t)ed->menu.line) : NULL;
    char file[ED_PATH_MAX], label[ED_PATH_MAX + 16];
    int line = 0;
    if (!br_ui_menu_begin(&ed->ui, "console")) return;
    if (l && ed_console_location(l->text, file, sizeof(file), &line)) {
        snprintf(label, sizeof(label), "Open %.40s:%d", br_path_name(file), line);
        if (item(ed, label, NULL, true)) ed_open_script_named(ed, file, line);
        sep(ed);
    }
    if (item(ed, "Copy Line", "Ctrl+C", l != NULL)) ed_console_copy(ed, ed->menu.line);
    if (item(ed, "Copy All", NULL, ed->console.count > 0)) ed_console_copy(ed, -1);
    sep(ed);
    if (item(ed, "Clear", NULL, ed->console.count > 0)) { ed_console_clear(&ed->console); ed->console_selected = -1; }
    if (br_ui_menu_check(&ed->ui, "Clear on Play", ed->console_clear_on_play)) ed->console_clear_on_play = !ed->console_clear_on_play;
    sep(ed);
    if (br_ui_menu_check(&ed->ui, "Show Messages", ed->console_show[ED_LEVEL_LOG])) ed->console_show[ED_LEVEL_LOG] = !ed->console_show[ED_LEVEL_LOG];
    if (br_ui_menu_check(&ed->ui, "Show Warnings", ed->console_show[ED_LEVEL_WARN])) ed->console_show[ED_LEVEL_WARN] = !ed->console_show[ED_LEVEL_WARN];
    if (br_ui_menu_check(&ed->ui, "Show Errors", ed->console_show[ED_LEVEL_ERROR])) ed->console_show[ED_LEVEL_ERROR] = !ed->console_show[ED_LEVEL_ERROR];
    br_ui_menu_end(&ed->ui);
}

/* Build, from the toolbar. */
static void build_menu(editor *ed) {
    ed_target host = ed_host_target(), t;
    char label[64], path[ED_PATH_MAX];
    if (!br_ui_menu_begin(&ed->ui, "build")) return;
    snprintf(label, sizeof(label), "Build for %s", ed_target_name(host));
    if (item(ed, label, "Ctrl+B", true)) ed_cmd_build(ed, host, false);
    if (item(ed, "Build and Run", "Ctrl+Shift+B", true)) ed_cmd_build(ed, host, true);
    if (item(ed, "Build and Open in Browser", NULL, ed_find_player(ed, ED_TARGET_WEB, path, sizeof(path)))) ed_cmd_build(ed, ED_TARGET_WEB, true);
    if (br_ui_menu_sub_begin(&ed->ui, "Build for", true)) {
        for (t = ED_TARGET_LINUX; t < ED_TARGET_COUNT; t = (ed_target)(t + 1)) {
            bool have = ed_find_player(ed, t, path, sizeof(path));
            if (t == ED_TARGET_WEB) snprintf(label, sizeof(label), have ? "Web" : "Web (run make web first)");
            else if (have || t == host) snprintf(label, sizeof(label), "%s", ed_target_name(t));
            else snprintf(label, sizeof(label), "%s (no player in players/)", ed_target_name(t));
            if (item(ed, label, NULL, have || t == host)) ed_cmd_build(ed, t, false);
        }
        br_ui_menu_sub_end(&ed->ui);
    }
    sep(ed);
    if (item(ed, "Check Scripts", NULL, true)) {
        int scripts = 0, errors;
        ed_project_reload(ed);
        errors = ed_check_scripts(ed, &scripts);
        if (errors) { ed_status(ed, "%d of %d scripts have mistakes - see the console", errors, scripts); ed->bottom_tab = ED_TAB_CONSOLE; ed_panel_set_open(ed, ED_PANEL_BOTTOM, true); }
        else ed_status(ed, "All %d script%s are fine%s", scripts, scripts == 1 ? "" : "s", ed->project.strict_typing ? " (strict typing)" : "");
    }
    if (item(ed, "Open Builds Folder", NULL, true)) ed_open_builds_folder(ed);
    if (item(ed, "Open Save Folder", NULL, true)) ed_open_save_folder(ed);
    if (item(ed, "Project Settings...", NULL, true)) ed_project_settings_open(ed);
    br_ui_menu_end(&ed->ui);
}

void ed_menus(editor *ed) {
    build_menu(ed);
    entity_menu(ed);
    hierarchy_menu(ed);
    view_menu(ed);
    asset_menu(ed);
    folder_menu(ed);
    component_menu(ed);
    inspector_menu(ed);
    console_menu(ed);
}
