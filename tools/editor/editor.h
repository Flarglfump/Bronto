/*
 * BRONTO EDITOR - a visual scene editor built on the engine.
 *
 *   doc.c        the DOCUMENT: the scene's entities, selection, undo/redo,
 *                play/stop, load/save. No drawing, no input; unit tested.
 *   commands.c   everything the user can do (New, Save, Delete, Play...),
 *                called by buttons and keyboard shortcuts alike
 *   viewport.c   the scene view: pan, zoom, grid, picking, move/rotate/scale
 *   panels.c     toolbar, hierarchy, inspector, status bar, dialogs
 *   assets.c     the asset browser: the project's files, previews, file
 *                operations, and dragging assets into the scene
 *   icon.c       the app icon, a little brontosaurus drawn in code
 *   settings.c   preferences (the script editor) and the Settings dialog
 *   main.c       engine setup, window layout, shortcuts
 *
 * The scene is an ordinary ECS world with the engine's built-in
 * components; the inspector is generated from their descriptions, and the
 * files it saves are the engine's scene format, which games load with
 * br_world_load_file().
 */
#ifndef BRONTO_EDITOR_H
#define BRONTO_EDITOR_H

#include "bronto/bronto.h"

#define ED_PATH_MAX 260
#define ED_UNDO_MAX 100
#define ED_MAX_COLLAPSED 256

/* ======================= doc.c: the document ======================= */

typedef struct ed_doc {
    br_world world;
    br_builtin_ids ids;
    br_entity *order;            /* hierarchy order = save order */
    uint32_t count, cap;
    br_entity selected;
    char path[ED_PATH_MAX];      /* "" = untitled */
    char root[ED_PATH_MAX];      /* the project folder asset paths are relative to */
    bool has_root;               /* false: each scene's own (br_assets_find_root) */
    char error[200];             /* why the last load/save failed */

    char *state;                 /* the scene as text at the last checkpoint */
    char *saved;                 /* ... when it was last saved or loaded */
    char *undo[ED_UNDO_MAX];
    int undo_count;
    char *redo[ED_UNDO_MAX];
    int redo_count;

    bool playing;
    char *play_snapshot;         /* restored by Stop */
    int play_selected;
} ed_doc;

void ed_doc_init(ed_doc *d);
void ed_doc_destroy(ed_doc *d);
void ed_doc_new(ed_doc *d);                       /* empty and untitled */
void ed_doc_mark_clean(ed_doc *d);                /* the current scene is the baseline: no undo, not dirty */
bool ed_doc_load(ed_doc *d, const char *path);    /* false: d->error says why */
bool ed_doc_save(ed_doc *d, const char *path);
bool ed_doc_dirty(const ed_doc *d);

br_entity ed_doc_create(ed_doc *d, const char *name, br_vec2 pos);   /* Name + Transform, a root */
br_entity ed_doc_create_child(ed_doc *d, br_entity parent, const char *name, br_vec2 local);
br_entity ed_doc_duplicate(ed_doc *d, br_entity e);                  /* with its subtree, placed after it */
void ed_doc_delete(ed_doc *d, br_entity e);                          /* with its subtree */
void ed_doc_reorder(ed_doc *d, br_entity e, int delta);              /* swap with the previous (-1) / next (+1) sibling */
void ed_doc_move(ed_doc *d, br_entity e, int slot);                  /* to before order[slot], as its sibling (count = the end) */
/* The tree: `order` is always depth first (each entity, then its subtree).
 * ed_doc_place makes `e` a child of `parent` (NULL: a root), keeping where it
 * is on screen, just before `before` (NULL: its new parent's last child). */
bool ed_doc_place(ed_doc *d, br_entity e, br_entity parent, br_entity before);
void ed_doc_normalize(ed_doc *d);
int  ed_doc_index(const ed_doc *d, br_entity e);
const char *ed_doc_name(const ed_doc *d, br_entity e, char *buf, size_t size);
br_entity ed_doc_pick(const ed_doc *d, br_vec2 world);               /* topmost under a point */

/* Fixes the project folder (the editor's asset browser shows it). */
void ed_doc_set_root(ed_doc *d, const char *root);
/* Prefabs are ordinary scene files. Instantiating one adds copies of its
 * entities at the end of the hierarchy, moved so that their average
 * position lands on `at`, and selects the first. Returns how many were
 * added, or -1 (d->error says why). */
int  ed_doc_instantiate(ed_doc *d, const char *path, br_vec2 at);
bool ed_doc_save_entity(ed_doc *d, br_entity e, const char *path);  /* one entity as a prefab */
int  ed_doc_count_entities(const char *path);                        /* in a scene file; -1 if unreadable */
/* Undo works on snapshots: call ed_doc_checkpoint() whenever nothing is
 * being dragged or typed. If the scene changed since the last checkpoint,
 * the old state becomes an undo step. */
void ed_doc_checkpoint(ed_doc *d);
bool ed_doc_undo(ed_doc *d);
bool ed_doc_redo(ed_doc *d);

void ed_doc_play(ed_doc *d);                      /* remember the scene, start the systems */
void ed_doc_stop(ed_doc *d);                      /* back to exactly what was remembered */
void ed_doc_step(ed_doc *d, float dt);            /* one fixed update while playing */

/* ======================= the editor ======================= */

typedef enum ed_drag { ED_DRAG_NONE = 0, ED_DRAG_PAN, ED_DRAG_MOVE, ED_DRAG_ROTATE, ED_DRAG_SCALE } ed_drag;

typedef struct ed_view {
    br_rect rect;                /* on screen, this frame */
    br_vec2 center;              /* world point in the middle of the view */
    float zoom;                  /* screen pixels per world unit */
    bool grid, snap;
    float grid_size;
    br_vec2 game_size;           /* the frame showing what a game's screen sees */
    ed_drag drag;
    br_vec2 drag_mouse, drag_offset;
    br_transform drag_start;
    float drag_angle, drag_dist;
    br_entity hover;
    br_vec2 mouse_world;
    bool mouse_inside;
} ed_view;

/* What the asset browser makes of a file. */
typedef enum ed_asset_kind {
    ED_ASSET_FOLDER = 0, ED_ASSET_IMAGE, ED_ASSET_SOUND, ED_ASSET_SCENE, ED_ASSET_SCRIPT, ED_ASSET_OTHER
} ed_asset_kind;

#define ED_ENTITY_DRAG "entity"          /* a hierarchy row; the payload is the entity handle */
#define ED_ASSET_DRAG "asset"            /* drag type; the payload is a project-relative path */

typedef struct ed_assets {
    char root[ED_PATH_MAX];              /* the project folder ("" = where the editor started) */
    char dir[ED_PATH_MAX];               /* the folder shown, relative to root ("" = root itself) */
    br_dir_list list;
    bool listed;
    double listed_at;                    /* listed again every so often, to see outside changes */
    char selected[ED_PATH_MAX];          /* relative path, "" = nothing */
    char name_field[128];                /* the preview's rename box */
    char name_for[ED_PATH_MAX];          /* the selection name_field was filled from */
    int scene_entities;                  /* cached for the preview of name_for */
    br_voice voice;                      /* a sound being previewed */
    char script_text[1200];              /* the selected script's first lines */
    char script_error[256];              /* "" = it parses */
    int64_t script_modified;
} ed_assets;

typedef enum ed_pending { ED_PENDING_NONE = 0, ED_PENDING_NEW, ED_PENDING_OPEN, ED_PENDING_QUIT, ED_PENDING_PROJECT } ed_pending;

/* The folder picker the editor draws itself, where the OS has no dialog. */
typedef struct ed_picker {
    bool open;
    char dir[ED_PATH_MAX];               /* absolute */
    br_dir_list list;
} ed_picker;

typedef struct editor {
    br_engine *engine;
    br_ui ui;
    ed_doc doc;
    ed_view view;
    ed_assets assets;
    char path_field[ED_PATH_MAX];      /* the toolbar's file name */
    bool section_closed[BR_MAX_COMPONENTS];   /* inspector sections folded away */
    int add_choice;                    /* the inspector's "Add component" list */
    ed_pending pending;                /* waiting on the unsaved-changes dialog */
    char pending_project[ED_PATH_MAX]; /* the folder Load Project will open */
    ed_picker picker;
    char status[200];
    double status_time;
    bool headless;
    br_image badge;                    /* the icon, small, for the toolbar */
    struct { char script_editor[256]; } prefs;   /* saved in the user's settings folder */
    char settings_field[256];
    bool settings_visible;
    br_entity collapsed[ED_MAX_COLLAPSED];   /* folded rows in the hierarchy */
    int collapsed_count;
    uint32_t script_errors_seen;       /* to report new script errors while playing */
    bool show_colliders;
} editor;

/* commands.c */
void ed_status(editor *ed, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
void ed_cmd_new(editor *ed);
void ed_cmd_load_project(editor *ed);      /* asks for a folder (the OS's dialog, or ed_picker) */
bool ed_open_project(editor *ed, const char *folder);   /* switch to it now, no questions */
void ed_choose_project(editor *ed, const char *folder); /* switch, asking about unsaved changes first */
void ed_cmd_open(editor *ed);              /* the file named in the toolbar */
void ed_cmd_save(editor *ed);
void ed_cmd_undo(editor *ed);
void ed_cmd_redo(editor *ed);
void ed_cmd_play(editor *ed);              /* toggles play / stop */
void ed_cmd_add_entity(editor *ed);
void ed_cmd_add_child(editor *ed);          /* under the selection */
void ed_cmd_duplicate(editor *ed);
void ed_cmd_delete(editor *ed);
void ed_cmd_nudge(editor *ed, br_vec2 delta);
void ed_cmd_frame(editor *ed);             /* centre the view on the selection */
bool ed_cmd_quit(editor *ed);              /* false: asked about unsaved changes first */
void ed_cmd_answer(editor *ed, int choice);/* dialog: 0 save, 1 discard, 2 cancel */
void ed_sample_scene(ed_doc *d);           /* what a fresh editor shows */
void ed_mark_project(editor *ed, const char *scene_path);   /* project.bronto, for scenes in subfolders */

/* viewport.c */
void ed_view_init(ed_view *v);
void ed_view_frame(editor *ed, br_rect rect);
br_vec2 ed_view_to_screen(const ed_view *v, br_vec2 world);
br_vec2 ed_view_to_world(const ed_view *v, br_vec2 screen);

/* icon.c: the brontosaurus at `size` x `size` pixels (transparent background) */
void ed_make_icon(br_image *out, int size);

/* settings.c */
void ed_prefs_load(editor *ed);
bool ed_prefs_save(editor *ed);
void ed_script_command(const char *tmpl, const char *file, int line, char *out, size_t size);
bool ed_open_script(editor *ed, const char *rel, int line);   /* in the chosen text editor */
void ed_settings_open(editor *ed);
void ed_settings_dialog(editor *ed, br_rect screen);

/* assets.c */
void ed_assets_init(editor *ed, const char *root);
void ed_assets_destroy(editor *ed);
ed_asset_kind ed_asset_kind_of(const char *name, bool is_dir);
void ed_asset_path(const editor *ed, const char *rel, char *out, size_t size);   /* rel -> openable path */
void ed_assets_open_dir(editor *ed, const char *rel);
void ed_assets_select(editor *ed, const char *rel);
void ed_assets_activate(editor *ed, const char *rel);      /* double click: enter, open, play, place */
void ed_assets_refresh(editor *ed);
bool ed_assets_new_folder(editor *ed);
bool ed_assets_rename(editor *ed, const char *new_name);   /* the selection */
bool ed_assets_delete(editor *ed);                         /* the selection, into <root>/.trash */
bool ed_assets_save_prefab(editor *ed);                    /* the selected entity, into the shown folder */
void ed_assets_play(editor *ed, const char *rel);          /* preview a sound (again: stop) */
/* Adds what an asset stands for at a world position: an image becomes a
 * sprite, a sound a Sound entity, a scene a prefab copy. */
bool ed_assets_drop(editor *ed, const char *rel, br_vec2 world);
bool ed_field_accepts(const br_component_desc *c, const br_field *f, const char *rel);   /* can a drop fill this string field? */
bool ed_assets_new_script(editor *ed);                    /* a template .bs in the shown folder */
bool ed_assets_edit(editor *ed, const char *rel);        /* open in the OS's editor */
void ed_assets_panel(editor *ed, br_rect rect);
void ed_assets_preview(editor *ed, br_rect rect);

/* panels.c */
void ed_toolbar(editor *ed, br_rect rect);
void ed_hierarchy(editor *ed, br_rect rect);
bool ed_collapsed(const editor *ed, br_entity e);
void ed_set_collapsed(editor *ed, br_entity e, bool collapsed);
void ed_inspector(editor *ed, br_rect rect);
void ed_statusbar(editor *ed, br_rect rect);
void ed_dialog(editor *ed, br_rect screen);
void ed_picker_dialog(editor *ed, br_rect screen);
void ed_picker_close(editor *ed);

#endif /* BRONTO_EDITOR_H */
