/*
 * BRONTO EDITOR - the asset browser.
 *
 * Shows the project folder as tiles: folders, images (with thumbnails),
 * sounds, scenes and other files. Click a tile to preview it on the right,
 * double-click to open it (a folder or a scene), hear it (a sound) or add
 * it to the scene (an image). Drag a tile into the scene view to place it,
 * or onto an inspector field (Sprite.image, Sound.file) to fill it in.
 *
 * Every asset is named by its path relative to the project folder, which
 * is also the asset cache's root, so the same text works in the editor,
 * in saved scenes and in games.
 *
 * Deleting never destroys anything: files go to <project>/.trash, which
 * the browser hides.
 */
#include "editor.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TILE_W 76.0f
#define TILE_H 70.0f
#define ICON 44.0f
#define RELIST_SECONDS 1.5
#define TRASH ".trash"

/* ---- paths and kinds ----------------------------------------------------------- */

ed_asset_kind ed_asset_kind_of(const char *name, bool is_dir) {
    if (is_dir) return ED_ASSET_FOLDER;
    if (br_path_has_ext(name, ".bmp") || br_path_has_ext(name, ".tga")) return ED_ASSET_IMAGE;
    if (br_path_has_ext(name, ".wav")) return ED_ASSET_SOUND;
    if (br_path_has_ext(name, ".scene")) return ED_ASSET_SCENE;
    if (br_path_has_ext(name, ".bs")) return ED_ASSET_SCRIPT;
    return ED_ASSET_OTHER;
}

static ed_asset_kind kind_of_rel(const editor *ed, const char *rel) {
    char full[ED_PATH_MAX];
    ed_asset_path(ed, rel, full, sizeof(full));
    return ed_asset_kind_of(rel, br_fs_is_dir(full));
}

void ed_asset_path(const editor *ed, const char *rel, char *out, size_t size) {
    br_path_join(out, size, ed->assets.root, rel);
}

/* A project-relative path always uses '/'. */
static void rel_join(char *out, size_t size, const char *dir, const char *name) {
    size_t a = strlen(dir), b = strlen(name), n = 0;
    if (size == 0) return;
    if (a >= size) a = size - 1;
    memcpy(out, dir, a);
    n = a;
    if (a && n + 1 < size) out[n++] = '/';
    if (b > size - 1 - n) b = size - 1 - n;      /* too long: cut (paths are far shorter) */
    memcpy(out + n, name, b);
    out[n + b] = 0;
}

/* "name.ext" -> "name (2).ext" ... until nothing by that name exists in `dir` (full path). */
static bool unique_name(char *out, size_t size, const char *dir, const char *name) {
    char stem[ED_PATH_MAX], full[ED_PATH_MAX];
    const char *ext = br_path_ext(name);
    int i;
    br_path_stem(stem, sizeof(stem), name);
    for (i = 1; i < 1000; ++i) {
        if (i == 1) snprintf(out, size, "%s", name);
        else snprintf(out, size, "%.80s (%d)%.20s", stem, i, ext);
        br_path_join(full, sizeof(full), dir, out);
        if (!br_fs_exists(full)) return true;
    }
    return false;
}

static void name_of(const char *rel, char *out, size_t size) { snprintf(out, size, "%s", br_path_name(rel)); }

/* ---- the listing ----------------------------------------------------------------- */

static void relist(editor *ed) {
    ed_assets *a = &ed->assets;
    char full[ED_PATH_MAX];
    br_dir_list_free(&a->list);
    ed_asset_path(ed, a->dir, full, sizeof(full));
    if (!br_fs_list(full, false, &a->list) && a->dir[0]) {   /* the folder went away: back to the top */
        a->dir[0] = 0;
        ed_asset_path(ed, a->dir, full, sizeof(full));
        br_fs_list(full, false, &a->list);
    }
    {   /* the project file is bookkeeping, not an asset */
        int i, n = 0;
        for (i = 0; i < a->list.count; ++i)
            if (strcmp(a->list.entries[i].name, BR_PROJECT_FILE) != 0) a->list.entries[n++] = a->list.entries[i];
        a->list.count = n;
    }
    a->listed = true;
    a->listed_at = ed->engine ? ed->engine->time : 0;
    if (a->selected[0]) {                     /* the selection was renamed or removed outside */
        char sel[ED_PATH_MAX];
        ed_asset_path(ed, a->selected, sel, sizeof(sel));
        if (!br_fs_exists(sel)) a->selected[0] = 0;
    }
}

void ed_assets_init(editor *ed, const char *root) {
    ed_assets *a = &ed->assets;
    memset(a, 0, sizeof(*a));
    snprintf(a->root, sizeof(a->root), "%s", root ? root : "");
    ed_doc_set_root(&ed->doc, a->root);
    a->scene_entities = -1;
    ed_project_reload(ed);                         /* its project.bronto: Build and strict typing */
}

void ed_assets_destroy(editor *ed) {
    if (ed->assets.voice && ed->engine) br_voice_stop(&ed->engine->mixer, ed->assets.voice);
    br_dir_list_free(&ed->assets.list);
}

void ed_assets_open_dir(editor *ed, const char *rel) {
    snprintf(ed->assets.dir, sizeof(ed->assets.dir), "%s", rel ? rel : "");
    ed->assets.selected[0] = 0;
    relist(ed);
}

void ed_assets_select(editor *ed, const char *rel) {
    snprintf(ed->assets.selected, sizeof(ed->assets.selected), "%s", rel ? rel : "");
}

void ed_assets_refresh(editor *ed) {
    if (ed->assets.voice) br_voice_stop(&ed->engine->mixer, ed->assets.voice);
    ed->assets.voice = 0;
    br_assets_clear();                       /* pick up files changed on disk */
    ed->assets.name_for[0] = 0;
    relist(ed);
}

/* ---- file operations --------------------------------------------------------------- */

bool ed_assets_new_folder(editor *ed) {
    ed_assets *a = &ed->assets;
    char dir[ED_PATH_MAX], name[128], full[ED_PATH_MAX], rel[ED_PATH_MAX];
    ed_asset_path(ed, a->dir, dir, sizeof(dir));
    if (!unique_name(name, sizeof(name), dir, "New folder")) return false;
    br_path_join(full, sizeof(full), dir, name);
    if (!br_fs_mkdir(full)) { ed_status(ed, "Couldn't make a folder in %s", dir[0] ? dir : "the project"); return false; }
    rel_join(rel, sizeof(rel), a->dir, name);
    relist(ed);
    ed_assets_select(ed, rel);
    ed_status(ed, "Made %s - type a new name in the preview to rename it", name);
    return true;
}

/* Keeps the open scene's path right when its file moves. */
static void follow_move(editor *ed, const char *from_full, const char *to_full) {
    if (strcmp(ed->doc.path, from_full) == 0) {
        snprintf(ed->doc.path, sizeof(ed->doc.path), "%s", to_full);
        snprintf(ed->path_field, sizeof(ed->path_field), "%s", to_full);
    }
}

bool ed_assets_rename(editor *ed, const char *new_name) {
    ed_assets *a = &ed->assets;
    char from[ED_PATH_MAX], parent[ED_PATH_MAX], to[ED_PATH_MAX], rel_parent[ED_PATH_MAX], rel[ED_PATH_MAX];
    if (!a->selected[0] || !new_name[0]) return false;
    if (strchr(new_name, '/') || strchr(new_name, '\\') || new_name[0] == '.') {
        ed_status(ed, "A name can't contain / or \\ or start with a dot");
        return false;
    }
    ed_asset_path(ed, a->selected, from, sizeof(from));
    br_path_parent(parent, sizeof(parent), from);
    br_path_join(to, sizeof(to), parent, new_name);
    if (strcmp(from, to) == 0) return true;
    if (br_fs_exists(to)) { ed_status(ed, "There already is a %s here", new_name); return false; }
    if (!br_fs_rename(from, to)) { ed_status(ed, "Couldn't rename %s", br_path_name(from)); return false; }
    br_assets_forget(a->selected);                   /* (stops it if it was playing) */
    follow_move(ed, from, to);
    br_path_parent(rel_parent, sizeof(rel_parent), a->selected);
    rel_join(rel, sizeof(rel), rel_parent, new_name);
    ed_status(ed, "Renamed %s to %s (scenes that use the old name won't find it)", br_path_name(a->selected), new_name);
    ed_assets_select(ed, rel);
    relist(ed);
    return true;
}

bool ed_assets_delete(editor *ed) {
    ed_assets *a = &ed->assets;
    char from[ED_PATH_MAX], trash[ED_PATH_MAX], name[128], to[ED_PATH_MAX];
    if (!a->selected[0]) { ed_status(ed, "Select a file or folder to delete"); return false; }
    ed_asset_path(ed, a->selected, from, sizeof(from));
    ed_asset_path(ed, TRASH, trash, sizeof(trash));
    if (!br_fs_mkdir(trash) || !unique_name(name, sizeof(name), trash, br_path_name(a->selected))) {
        ed_status(ed, "Couldn't make the %s folder", TRASH);
        return false;
    }
    br_path_join(to, sizeof(to), trash, name);
    br_assets_forget(a->selected);
    if (!br_fs_rename(from, to)) { ed_status(ed, "Couldn't move %s to the trash", br_path_name(from)); return false; }
    follow_move(ed, from, to);
    ed_status(ed, "Moved %s to %s", br_path_name(from), TRASH);
    a->selected[0] = 0;
    relist(ed);
    return true;
}

bool ed_assets_save_prefab(editor *ed) {
    ed_doc *d = &ed->doc;
    char base[64], file[96], name[128], dir[ED_PATH_MAX], full[ED_PATH_MAX], rel[ED_PATH_MAX];
    size_t i;
    if (!br_entity_alive(&d->world, d->selected)) { ed_status(ed, "Select an entity to save it as a prefab"); return false; }
    if (d->playing) { ed_status(ed, "Stop playing first"); return false; }
    ed_doc_name(d, d->selected, base, sizeof(base));
    for (i = 0; base[i]; ++i)                         /* keep file names portable */
        if (strchr("/\\:*?\"<>|", base[i])) base[i] = '_';
    snprintf(file, sizeof(file), "%s.scene", base);
    ed_asset_path(ed, ed->assets.dir, dir, sizeof(dir));
    if (!unique_name(name, sizeof(name), dir, file)) return false;
    br_path_join(full, sizeof(full), dir, name);
    if (!ed_doc_save_entity(d, d->selected, full)) { ed_status(ed, "Couldn't save the prefab: %s", d->error); return false; }
    ed_mark_project(ed, full);
    rel_join(rel, sizeof(rel), ed->assets.dir, name);
    relist(ed);
    ed_assets_select(ed, rel);
    ed_status(ed, "Saved prefab %s - drag it into the scene to place copies", rel);
    return true;
}

void ed_assets_play(editor *ed, const char *rel) {
    ed_assets *a = &ed->assets;
    br_mixer *m = &ed->engine->mixer;
    const br_sound *s;
    if (a->voice && br_voice_playing(m, a->voice)) {
        br_voice_stop(m, a->voice);
        a->voice = 0;
        return;
    }
    if (!(s = br_asset_sound(rel))) { ed_status(ed, "Couldn't load %s as a WAV sound", rel); return; }
    a->voice = br_play(m, s, NULL);
    if (!ed->engine->config.audio_sample_rate) ed_status(ed, "Audio is off");
}

/* ---- adding assets to the scene ------------------------------------------------------- */

bool ed_field_accepts(const br_component_desc *c, const br_field *f, const char *rel) {
    ed_asset_kind k = ed_asset_kind_of(rel, false);
    if (f->type != BR_FIELD_STRING) return false;
    if (strcmp(f->name, "image") == 0) return k == ED_ASSET_IMAGE;
    if (strcmp(f->name, "file") == 0) return strcmp(c->name, "Script") == 0 ? k == ED_ASSET_SCRIPT : k == ED_ASSET_SOUND;
    return false;
}

static const char k_script_template[] =
    "# %s - a script. Attach it to an entity with a Script component\n"
    "# (or drag this file onto the entity in the scene view).\n"
    "\n"
    "var speed = 60            # variables keep their value between calls\n"
    "\n"
    "func start() {\n"
    "    # runs once, when the scene starts\n"
    "}\n"
    "\n"
    "func update(dt) {\n"
    "    # runs every step; dt is the time since the last one (seconds)\n"
    "    if key_down(\"right\") { self.x += speed * dt }\n"
    "    if key_down(\"left\") { self.x -= speed * dt }\n"
    "}\n"
    "\n"
    "func on_collision(other) {\n"
    "    # another collider touched this one (needs a Collider)\n"
    "}\n";

bool ed_assets_new_script(editor *ed) {
    char dir[ED_PATH_MAX], name[128], full[ED_PATH_MAX], rel[ED_PATH_MAX], text[2048];
    ed_asset_path(ed, ed->assets.dir, dir, sizeof(dir));
    if (!unique_name(name, sizeof(name), dir, "new_script.bs")) return false;
    br_path_join(full, sizeof(full), dir, name);
    snprintf(text, sizeof(text), k_script_template, name);
    if (!br_file_write_all(full, text, strlen(text))) { ed_status(ed, "Couldn't write %s", name); return false; }
    rel_join(rel, sizeof(rel), ed->assets.dir, name);
    relist(ed);
    ed_assets_select(ed, rel);
    ed_status(ed, "Made %s - drag it onto an entity, or double-click to edit it", name);
    return true;
}

bool ed_assets_edit(editor *ed, const char *rel) {
    int line = 1;
    const char *colon;
    if (strcmp(ed->assets.selected, rel) == 0 && ed->assets.script_error[0] &&
        (colon = strchr(ed->assets.script_error, ':')) != NULL) line = atoi(colon + 1);   /* "player.bs:12: ..." */
    return ed_open_script(ed, rel, line);
}

/* A script dropped on an entity is attached to it; anywhere else it gets a new entity. */
static bool drop_script(editor *ed, const char *rel, br_vec2 world) {
    ed_doc *d = &ed->doc;
    br_component sc = br_component_find(&d->world, "Script");
    br_entity e = ed_doc_pick(d, world);
    char stem[32], name[64];
    br_script *s;
    bool fresh = e == BR_ENTITY_NULL;
    if (sc == BR_COMPONENT_INVALID) return false;
    br_path_stem(stem, sizeof(stem), rel);
    if (fresh && (e = ed_doc_create(d, stem, world)) == BR_ENTITY_NULL) return false;
    s = (br_script *)br_add(&d->world, e, sc, br_get(&d->world, e, sc));   /* keeps its vars if it had one */
    snprintf(s->file, sizeof(s->file), "%s", rel);
    d->selected = e;
    ed_status(ed, "%s %s to %s", fresh ? "Added" : "Attached", rel, ed_doc_name(d, e, name, sizeof(name)));
    return true;
}

bool ed_assets_drop(editor *ed, const char *rel, br_vec2 world) {
    ed_doc *d = &ed->doc;
    char stem[32], full[ED_PATH_MAX];
    br_entity e;
    ed_asset_kind k = kind_of_rel(ed, rel);
    if (d->playing) { ed_status(ed, "Stop playing to add things to the scene"); return false; }
    if (ed->view.snap) {
        world.x = roundf(world.x / ed->view.grid_size) * ed->view.grid_size;
        world.y = roundf(world.y / ed->view.grid_size) * ed->view.grid_size;
    }
    br_path_stem(stem, sizeof(stem), rel);
    switch (k) {
    case ED_ASSET_IMAGE:
        if ((e = ed_doc_create(d, stem, world)) == BR_ENTITY_NULL) return false;
        snprintf(((br_sprite *)br_add(&d->world, e, d->ids.sprite, NULL))->image, sizeof(((br_sprite *)0)->image), "%s", rel);
        ed_status(ed, "Added sprite %s", stem);
        return true;
    case ED_ASSET_SOUND:
        if ((e = ed_doc_create(d, stem, world)) == BR_ENTITY_NULL) return false;
        snprintf(((br_sound_src *)br_add(&d->world, e, d->ids.sound, NULL))->file, sizeof(((br_sound_src *)0)->file), "%s", rel);
        ed_status(ed, "Added sound %s (it plays when the scene starts)", stem);
        return true;
    case ED_ASSET_SCENE: {
        int n;
        ed_asset_path(ed, rel, full, sizeof(full));
        if (d->path[0] && strcmp(full, d->path) == 0) { ed_status(ed, "A scene can't be placed inside itself"); return false; }
        n = ed_doc_instantiate(d, full, world);
        if (n < 0) { ed_status(ed, "Couldn't place %s: %s", rel, d->error); return false; }
        ed_status(ed, "Placed %d entit%s from %s", n, n == 1 ? "y" : "ies", rel);
        return n > 0;
    }
    case ED_ASSET_SCRIPT:
        return drop_script(ed, rel, world);
    default:
        ed_status(ed, "%s isn't something the scene can use", br_path_name(rel));
        return false;
    }
}

void ed_assets_activate(editor *ed, const char *rel) {
    char full[ED_PATH_MAX];
    switch (kind_of_rel(ed, rel)) {
    case ED_ASSET_FOLDER: ed_assets_open_dir(ed, rel); break;
    case ED_ASSET_SCENE:
        ed_asset_path(ed, rel, full, sizeof(full));
        snprintf(ed->path_field, sizeof(ed->path_field), "%s", full);
        ed_cmd_open(ed);
        break;
    case ED_ASSET_SOUND: ed_assets_play(ed, rel); break;
    case ED_ASSET_IMAGE: ed_assets_drop(ed, rel, ed->view.center); break;
    case ED_ASSET_SCRIPT: ed_assets_edit(ed, rel); break;
    default: break;
    }
}

/* ---- drawing ----------------------------------------------------------------------------- */

/* `text` shortened with ".." to fit `width` pixels at scale 1. */
static void fit_text(const char *text, float width, char *out, size_t size) {
    size_t n = strlen(text), max = (size_t)(width / 8.0f);
    if (n <= max || max < 3) { snprintf(out, size, "%s", text); return; }
    snprintf(out, size, "%.*s..", (int)(max - 2), text);
}

static void draw_image_fit(br_renderer *r, const br_image *img, br_rect box, float max_scale) {
    float s = BR_MIN(box.w / (float)img->width, box.h / (float)img->height);
    if (s > max_scale) s = max_scale;
    if (s >= 1) s = floorf(s);                       /* crisp pixels when enlarging */
    br_draw_sprite(r, img, NULL, br_v2(floorf(box.x + box.w / 2), floorf(box.y + box.h / 2)),
                   br_v2((float)img->width / 2, (float)img->height / 2), br_v2(s, s), 0, BR_WHITE);
}

static void draw_icon(br_renderer *r, ed_asset_kind k, br_rect b) {
    float cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    switch (k) {
    case ED_ASSET_FOLDER:
        br_fill_rect(r, br_rect_make(cx - 18, cy - 12, 14, 5), br_rgb(214, 170, 72));
        br_fill_rect(r, br_rect_make(cx - 18, cy - 8, 36, 22), br_rgb(240, 196, 92));
        br_draw_rect(r, br_rect_make(cx - 18, cy - 8, 36, 22), br_rgb(170, 128, 50));
        break;
    case ED_ASSET_SOUND:
        br_fill_rect(r, br_rect_make(cx - 14, cy - 5, 8, 10), br_rgb(120, 220, 170));
        br_fill_triangle(r, br_v2(cx - 7, cy - 5), br_v2(cx + 3, cy - 13), br_v2(cx + 3, cy + 13), br_rgb(120, 220, 170));
        br_fill_triangle(r, br_v2(cx - 7, cy - 5), br_v2(cx + 3, cy + 13), br_v2(cx - 7, cy + 5), br_rgb(120, 220, 170));
        br_draw_line(r, br_v2(cx + 8, cy - 5), br_v2(cx + 10, cy), br_rgb(120, 220, 170));
        br_draw_line(r, br_v2(cx + 10, cy), br_v2(cx + 8, cy + 5), br_rgb(120, 220, 170));
        br_draw_line(r, br_v2(cx + 12, cy - 9), br_v2(cx + 15, cy), br_rgb(120, 220, 170));
        br_draw_line(r, br_v2(cx + 15, cy), br_v2(cx + 12, cy + 9), br_rgb(120, 220, 170));
        break;
    case ED_ASSET_SCENE:
        br_fill_rect(r, br_rect_make(cx - 17, cy - 13, 34, 26), br_rgb(40, 60, 96));
        br_draw_rect(r, br_rect_make(cx - 17, cy - 13, 34, 26), br_rgb(90, 160, 255));
        br_fill_circle(r, br_v2(cx - 6, cy + 2), 5, br_rgb(255, 140, 50));
        br_fill_rect(r, br_rect_make(cx + 2, cy - 7, 10, 10), br_rgb(170, 90, 230));
        br_fill_triangle(r, br_v2(cx + 4, cy + 10), br_v2(cx + 9, cy + 3), br_v2(cx + 14, cy + 10), br_rgb(90, 220, 110));
        break;
    case ED_ASSET_SCRIPT:             /* a page of code */
        br_fill_rect(r, br_rect_make(cx - 13, cy - 16, 26, 32), br_rgb(46, 52, 64));
        br_draw_rect(r, br_rect_make(cx - 13, cy - 16, 26, 32), br_rgb(150, 200, 255));
        br_draw_text(r, "{}", cx - 8, cy - 12, 1, br_rgb(255, 210, 90));
        br_draw_line(r, br_v2(cx - 8, cy), br_v2(cx + 8, cy), br_rgb(150, 200, 255));
        br_draw_line(r, br_v2(cx - 4, cy + 5), br_v2(cx + 9, cy + 5), br_rgb(130, 220, 150));
        br_draw_line(r, br_v2(cx - 4, cy + 10), br_v2(cx + 5, cy + 10), br_rgb(150, 200, 255));
        break;
    case ED_ASSET_IMAGE:              /* an image that won't load */
        br_fill_rect(r, br_rect_make(cx - 14, cy - 14, 28, 28), br_rgba(255, 0, 255, 90));
        br_draw_line(r, br_v2(cx - 14, cy - 14), br_v2(cx + 14, cy + 14), br_rgb(255, 0, 255));
        br_draw_line(r, br_v2(cx + 14, cy - 14), br_v2(cx - 14, cy + 14), br_rgb(255, 0, 255));
        break;
    default:
        br_fill_rect(r, br_rect_make(cx - 12, cy - 16, 24, 32), br_rgb(200, 205, 215));
        br_fill_triangle(r, br_v2(cx + 4, cy - 16), br_v2(cx + 12, cy - 8), br_v2(cx + 4, cy - 8), br_rgb(150, 156, 168));
        br_draw_line(r, br_v2(cx - 7, cy - 2), br_v2(cx + 7, cy - 2), br_rgb(150, 156, 168));
        br_draw_line(r, br_v2(cx - 7, cy + 3), br_v2(cx + 7, cy + 3), br_rgb(150, 156, 168));
        br_draw_line(r, br_v2(cx - 7, cy + 8), br_v2(cx + 4, cy + 8), br_rgb(150, 156, 168));
        break;
    }
}

static void draw_thumbnail(br_renderer *r, const char *rel, ed_asset_kind k, br_rect box, float max_scale) {
    const br_image *img = k == ED_ASSET_IMAGE ? br_asset_image(rel) : NULL;
    if (img) {
        int x, y;                                  /* a checkerboard shows transparency */
        for (y = 0; y < (int)box.h; y += 6)
            for (x = 0; x < (int)box.w; x += 6)
                br_fill_rect(r, br_rect_make(box.x + (float)x, box.y + (float)y, BR_MIN(6.0f, box.w - (float)x), BR_MIN(6.0f, box.h - (float)y)),
                             ((x + y) / 6) % 2 ? br_rgb(58, 62, 70) : br_rgb(48, 52, 60));
        draw_image_fit(r, img, box, max_scale);
    } else {
        draw_icon(r, k, box);
    }
}

/* ---- the browser panel ---------------------------------------------------------------------- */

/* "Project > art > enemies": a button for each folder on the way down. */
static void breadcrumb(editor *ed) {
    br_ui *ui = &ed->ui;
    br_rect row = br_ui_row(ui, 0);
    char part[ED_PATH_MAX], target[ED_PATH_MAX], label[ED_PATH_MAX + 16];
    const char *p = ed->assets.dir;
    float x = row.x, w;
    int i;
    target[0] = 0;
    for (i = 0;; ++i) {
        if (i == 0) {
            char abs[ED_PATH_MAX];                     /* the project's own folder name */
            snprintf(part, sizeof(part), "%s", br_fs_absolute(ed->assets.root, abs, sizeof(abs)) ? br_path_name(abs) : "Project");
            if (!part[0]) snprintf(part, sizeof(part), "Project");
        } else {
            const char *slash = strchr(p, '/');
            size_t n = slash ? (size_t)(slash - p) : strlen(p);
            size_t upto = (size_t)(p + n - ed->assets.dir);       /* the path up to this folder */
            snprintf(part, sizeof(part), "%.*s", (int)n, p);
            memcpy(target, ed->assets.dir, upto);
            target[upto] = 0;
            p = slash ? slash + 1 : p + n;
        }
        w = br_measure_text(part, ui->style.font).x + 12;
        if (x + w > row.x + row.w) break;
        snprintf(label, sizeof(label), "%.200s##crumb%d", part, i);
        br_ui_place(ui, br_rect_make(x, row.y, w, row.h));
        if (br_ui_button(ui, label)) { ed_assets_open_dir(ed, target); return; }
        x += w;
        if (!*p) break;
        br_draw_text(ui->r, ">", x + 3, row.y + 4, 1, ui->style.text_dim);
        x += 14;
    }
}

/* One tile; true when it was double-clicked (acted on after the loop: it may change the listing). */
static bool tile(editor *ed, const br_dir_entry *ent, br_rect box) {
    br_ui *ui = &ed->ui;
    ed_assets *a = &ed->assets;
    char rel[ED_PATH_MAX], label[ED_PATH_MAX + 4], shown[ED_PATH_MAX];
    ed_asset_kind k = ed_asset_kind_of(ent->name, ent->is_dir);
    bool selected;
    int f;
    rel_join(rel, sizeof(rel), a->dir, ent->name);
    snprintf(label, sizeof(label), "##%s", ent->name);
    br_ui_place(ui, box);
    f = br_ui_area(ui, label, 0);
    selected = strcmp(a->selected, rel) == 0;
    if (selected) br_fill_rect(ui->r, box, ui->style.selection);
    else if (f & BR_UI_HOVERED) br_fill_rect(ui->r, box, ui->style.header);
    draw_thumbnail(ui->r, rel, k, br_rect_make(box.x + (box.w - ICON) / 2, box.y + 3, ICON, ICON), 4);
    fit_text(ent->name, box.w - 4, shown, sizeof(shown));
    {
        float w = br_measure_text(shown, 1).x;
        br_draw_text(ui->r, shown, floorf(box.x + (box.w - w) / 2), box.y + ICON + 9, 1, selected ? BR_WHITE : ui->style.text);
    }
    if (f & BR_UI_PRESSED) ed_assets_select(ed, rel);
    if (f & BR_UI_RIGHT_CLICKED) ed_menu_asset(ed, rel);
    if (k != ED_ASSET_FOLDER && k != ED_ASSET_OTHER)       /* (scripts too: drop onto an entity) */
        br_ui_drag_source(ui, ED_ASSET_DRAG, rel, ent->name);
    return (f & BR_UI_DOUBLE_CLICKED) != 0;
}

void ed_assets_panel(editor *ed, br_rect rect) {
    br_ui *ui = &ed->ui;
    ed_assets *a = &ed->assets;
    int i, per_row, n, activate = -1;
    if (!a->listed || (ed->engine->time - a->listed_at > RELIST_SECONDS && !br_ui_dragging(ui, NULL))) relist(ed);
    br_ui_panel_begin(ui, "assets", rect);
    br_ui_columns(ui, 6);
    if (br_ui_button(ui, "Up")) {
        char up[ED_PATH_MAX];
        br_path_parent(up, sizeof(up), a->dir);
        ed_assets_open_dir(ed, up);
    }
    if (br_ui_button(ui, "+ Folder")) ed_assets_new_folder(ed);
    if (br_ui_button(ui, "Delete##asset")) ed_assets_delete(ed);
    if (br_ui_button(ui, "Refresh")) { ed_assets_refresh(ed); ed_status(ed, "Reloaded the folder and every asset"); }
    if (br_ui_button(ui, "To prefab")) ed_assets_save_prefab(ed);
    if (br_ui_button(ui, "+ Script")) ed_assets_new_script(ed);
    breadcrumb(ed);
    br_ui_spacing(ui, 2);
    n = a->list.count;
    per_row = (int)((rect.w - 2 * ui->style.padding - 6) / TILE_W);
    if (per_row < 1) per_row = 1;
    for (i = 0; i < n; i += per_row) {                  /* one UI row per row of tiles */
        br_rect row = br_ui_row(ui, TILE_H);
        int j;
        for (j = 0; j < per_row && i + j < n; ++j)
            if (tile(ed, &a->list.entries[i + j], br_rect_make(row.x + (float)j * TILE_W, row.y, TILE_W - 2, TILE_H)))
                activate = i + j;
    }
    if (activate >= 0) {
        char rel[ED_PATH_MAX];
        rel_join(rel, sizeof(rel), a->dir, a->list.entries[activate].name);
        ed_assets_activate(ed, rel);
    }
    if (n == 0) br_ui_label_colored(ui, "This folder is empty. Copy images (.bmp .tga), sounds (.wav) or scenes here.", ui->style.text_dim);
    if (br_ui_context_area(ui, rect)) ed_menu_asset(ed, "");
    br_ui_panel_end(ui);
}

/* ---- the preview panel ------------------------------------------------------------------------ */

/* The selected script's first lines and whether it parses, re-read when the file changes. */
static void sync_script(editor *ed) {
    ed_assets *a = &ed->assets;
    char full[ED_PATH_MAX];
    br_dir_entry info;
    char *text;
    size_t size, n = 0;
    int lines = 0;
    ed_asset_path(ed, a->selected, full, sizeof(full));
    if (!br_fs_stat(full, &info) || info.modified == a->script_modified) return;
    a->script_modified = info.modified;
    a->script_text[0] = a->script_error[0] = 0;
    if (!(text = (char *)br_file_read_all(full, &size))) return;
    br_script_check(text, a->selected, a->script_error, sizeof(a->script_error));   /* "" when it parses */
    while (n < size && n + 1 < sizeof(a->script_text) && lines < 12) {
        if (text[n] == '\n') ++lines;
        a->script_text[n] = text[n] == '\t' ? ' ' : text[n];
        ++n;
    }
    a->script_text[n] = 0;
    br_free(text);
}

static void sync_name_field(editor *ed) {
    ed_assets *a = &ed->assets;
    if (a->selected[0] && ed_asset_kind_of(a->selected, false) == ED_ASSET_SCRIPT) sync_script(ed);
    if (strcmp(a->name_for, a->selected) == 0) return;
    a->script_modified = -1;
    snprintf(a->name_for, sizeof(a->name_for), "%s", a->selected);
    name_of(a->selected, a->name_field, sizeof(a->name_field));
    a->scene_entities = -1;
    if (a->selected[0] && ed_asset_kind_of(a->selected, false) == ED_ASSET_SCENE) {
        char full[ED_PATH_MAX];
        ed_asset_path(ed, a->selected, full, sizeof(full));
        a->scene_entities = ed_doc_count_entities(full);
    }
}

void ed_assets_preview(editor *ed, br_rect rect) {
    br_ui *ui = &ed->ui;
    ed_assets *a = &ed->assets;
    char full[ED_PATH_MAX];
    br_dir_entry info;
    ed_asset_kind k;
    br_ui_panel_begin(ui, "preview", rect);
    sync_name_field(ed);
    if (!a->selected[0]) {
        br_ui_heading(ui, "Preview");
        br_ui_label_colored(ui, "Click a file to see it here.", ui->style.text_dim);
        br_ui_label_colored(ui, "Drag images, sounds and", ui->style.text_dim);
        br_ui_label_colored(ui, "scenes into the scene view.", ui->style.text_dim);
        br_ui_label_colored(ui, "Double-click: open / play.", ui->style.text_dim);
        br_ui_panel_end(ui);
        return;
    }
    ed_asset_path(ed, a->selected, full, sizeof(full));
    if (!br_fs_stat(full, &info)) { a->selected[0] = 0; br_ui_panel_end(ui); return; }
    k = ed_asset_kind_of(a->selected, info.is_dir);
    if (a->focus_name) { br_ui_focus_next(ui); a->focus_name = false; }   /* Rename */
    if (br_ui_text_field(ui, "Name##asset", a->name_field, sizeof(a->name_field))) {
        if (!ed_assets_rename(ed, a->name_field)) name_of(a->selected, a->name_field, sizeof(a->name_field));
        snprintf(a->name_for, sizeof(a->name_for), "%s", a->selected);
    }
    if (k == ED_ASSET_IMAGE || k == ED_ASSET_SCENE) {
        br_rect box = br_ui_row(ui, 72);
        br_fill_rect(ui->r, box, ui->style.field);
        draw_thumbnail(ui->r, a->selected, k, br_rect_make(box.x + 2, box.y + 2, box.w - 4, box.h - 4), 8);
    }
    switch (k) {
    case ED_ASSET_FOLDER:
        br_ui_label_colored(ui, "Folder", ui->style.text_dim);
        if (br_ui_button(ui, "Open folder")) ed_assets_open_dir(ed, a->selected);
        break;
    case ED_ASSET_IMAGE: {
        const br_image *img = br_asset_image(a->selected);
        if (img) br_ui_labelf(ui, "Image  %d x %d", img->width, img->height);
        else br_ui_label_colored(ui, "Can't read this image", br_rgb(255, 120, 120));
        if (br_ui_button(ui, "Add to scene")) ed_assets_drop(ed, a->selected, ed->view.center);
        break;
    }
    case ED_ASSET_SOUND: {
        const br_sound *s = br_asset_sound(a->selected);
        bool playing = a->voice && br_voice_playing(&ed->engine->mixer, a->voice);
        if (s) br_ui_labelf(ui, "Sound  %.2f s  %s  %d Hz", (double)br_sound_duration(s), s->channels == 2 ? "stereo" : "mono", s->sample_rate);
        else br_ui_label_colored(ui, "Can't read this sound", br_rgb(255, 120, 120));
        br_ui_columns(ui, 2);
        if (br_ui_toggle(ui, playing ? "Stop" : "Play", playing)) ed_assets_play(ed, a->selected);
        if (br_ui_button(ui, "Add to scene")) ed_assets_drop(ed, a->selected, ed->view.center);
        break;
    }
    case ED_ASSET_SCENE:
        if (a->scene_entities >= 0) br_ui_labelf(ui, "Scene  %d entit%s", a->scene_entities, a->scene_entities == 1 ? "y" : "ies");
        else br_ui_label_colored(ui, "Can't read this scene", br_rgb(255, 120, 120));
        br_ui_columns(ui, 2);
        if (br_ui_button(ui, "Open")) ed_assets_activate(ed, a->selected);
        if (br_ui_button(ui, "Place")) ed_assets_drop(ed, a->selected, ed->view.center);
        break;
    case ED_ASSET_SCRIPT: {
        const char *p = a->script_text;
        int shown = 0;
        sync_script(ed);
        br_ui_columns(ui, 2);
        if (br_ui_button(ui, "Edit")) ed_assets_edit(ed, a->selected);
        if (br_ui_button(ui, "Attach")) {
            if (br_entity_alive(&ed->doc.world, ed->doc.selected)) {
                br_transform wt = br_world_transform(&ed->doc.world, ed->doc.selected);
                ed_assets_drop(ed, a->selected, wt.position);
            } else {
                ed_status(ed, "Select an entity first, or drag the script onto one");
            }
        }
        if (a->script_error[0]) br_ui_label_colored(ui, a->script_error, br_rgb(255, 120, 120));
        else br_ui_label_colored(ui, "Script  (no mistakes found)", br_rgb(130, 220, 150));
        while (*p && shown < 5) {                       /* the first lines, dimmed */
            char line[64];
            size_t len = strcspn(p, "\n");
            snprintf(line, sizeof(line), "%.*s", (int)(len < 40 ? len : 40), p);
            br_ui_label_colored(ui, line, ui->style.text_dim);
            p += len + (p[len] ? 1 : 0);
            ++shown;
        }
        break;
    }
    default:
        br_ui_labelf(ui, "File  %llu bytes", (unsigned long long)info.size);
        break;
    }
    if (k != ED_ASSET_SCRIPT) br_ui_label_colored(ui, a->selected, ui->style.text_dim);
    br_ui_panel_end(ui);
}
