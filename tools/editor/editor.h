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
 *   console.c    the console's lines: captured stdout / stderr, the log, print()
 *   menus.c      the right-click menus, and copy / paste of entities
 *   build.c      the Build menu (games: the player with the project packed in), Project Settings
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
/* Copy and paste: an entity and its subtree as scene text (br_free it), and
 * that text added back - at `at` (NULL: where it was), under `parent`
 * (NULL: a root), its first entity selected. Returns how many were added, or -1. */
char *ed_doc_copy(ed_doc *d, br_entity e);
int  ed_doc_paste(ed_doc *d, const char *text, const br_vec2 *at, br_entity parent);
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
bool ed_doc_switch_scene(ed_doc *d, const char *path);   /* while playing: a script's load_scene() */

/* ======================= console.c: the console's lines ======================= */

#define ED_CONSOLE_LINES 2000
#define ED_CONSOLE_TEXT 200

typedef enum ed_con_kind {
    ED_CON_OUT = 0,      /* stdout */
    ED_CON_ERR,          /* stderr that isn't a log line */
    ED_CON_PRINT,        /* print() in a script */
    ED_CON_INFO, ED_CON_WARN, ED_CON_ERROR    /* the engine's log */
} ed_con_kind;
enum { ED_LEVEL_LOG = 0, ED_LEVEL_WARN, ED_LEVEL_ERROR };

typedef struct ed_con_line {
    char text[ED_CONSOLE_TEXT];
    uint32_t repeat;             /* the same line, this many times in a row */
    double time;                 /* engine time of the last one */
    uint8_t kind, level;
} ed_con_line;

typedef struct ed_console {
    ed_con_line lines[ED_CONSOLE_LINES];   /* a ring: the oldest goes when full */
    uint32_t first, count;
    uint32_t level_count[3];     /* lines (with repeats) per ED_LEVEL_* */
    uint64_t added;              /* changes whenever a line comes in */
    char partial[3][ED_CONSOLE_TEXT];      /* per stream (1 stdout, 2 stderr): a line not finished yet */
    size_t partial_len[3];
    double partial_time[3];
} ed_console;

void ed_console_clear(ed_console *c);
void ed_console_add(ed_console *c, ed_con_kind kind, const char *text, double now);    /* one line or several */
void ed_console_feed(ed_console *c, int stream, const char *bytes, size_t n, double now);  /* captured pieces */
void ed_console_flush(ed_console *c, double now, double max_wait);   /* unfinished lines older than max_wait */
const ed_con_line *ed_console_line(const ed_console *c, uint32_t i);  /* 0 = the oldest */
/* A script location ("scripts/player.bs:12") in a line of text. */
bool ed_console_location(const char *text, char *file, size_t size, int *line);

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
    br_mouse_button pan_button;  /* what started the pan: a right click without a drag opens the menu */
    br_vec2 press_at;
    bool pan_moved;
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
    bool focus_name;                     /* Rename: the preview's name box takes the keyboard */
} ed_assets;

typedef enum ed_bottom_tab { ED_TAB_FILES = 0, ED_TAB_CONSOLE } ed_bottom_tab;

/* The panels around the scene view, each resized by dragging its edge and
 * collapsed with its arrow (main.c lays them out; saved with the settings). */
typedef enum ed_panel {
    ED_PANEL_SCENE = 0,                /* the hierarchy, on the left */
    ED_PANEL_INSPECTOR,                /* on the right */
    ED_PANEL_BOTTOM,                   /* Files / Console, under the view */
    ED_PANEL_PREVIEW,                  /* beside the files */
    ED_PANEL_COUNT
} ed_panel;
typedef struct ed_panels {
    float size[ED_PANEL_COUNT];        /* width (height for the bottom) while open */
    bool open[ED_PANEL_COUNT];
} ed_panels;

typedef enum ed_target { ED_TARGET_LINUX = 0, ED_TARGET_MACOS, ED_TARGET_WINDOWS, ED_TARGET_WEB, ED_TARGET_COUNT } ed_target;

/* What a right-click menu is about (menus.c). */
typedef struct ed_menu_target {
    br_entity entity;                    /* hierarchy row / scene view */
    br_vec2 world;                       /* where the scene view was clicked */
    char asset[ED_PATH_MAX];             /* a file or folder ("" = the folder shown) */
    br_component component;              /* an inspector section */
    int field;                           /* ... and its field (-1: the whole component) */
    int line;                            /* a console line (index, -1: none) */
} ed_menu_target;

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

    ed_bottom_tab bottom_tab;          /* Files or Console under the scene view */
    ed_panels panels;
    int split_drag;                    /* the panel whose edge is being dragged, -1: none */
    float split_grab, split_start;     /* where on the edge it was taken; the size then */
    int split_hot;                     /* the edge under the mouse (drawn lit), -1: none */
    bool chrome_clicked;               /* an arrow or edge took this frame's click (a strip under it mustn't) */
    ed_console console;
    bool console_capturing;            /* stdout / stderr come to the console */
    bool console_show[3];              /* the filters, per ED_LEVEL_* */
    bool console_clear_on_play;
    char console_search[64];
    uint64_t console_seen;             /* console.added when the Console tab was last shown */
    uint32_t console_errors_seen;      /* errors counted then */
    int console_selected;              /* a line, -1: none */

    ed_menu_target menu;               /* what the open right-click menu is about */
    char *clipboard;                   /* copied entities, as scene text (also on the system clipboard) */
    struct {                           /* a copied component's values */
        char name[32];
        unsigned char data[1024];
        bool full;
    } component_clip;
    bool focus_name;                   /* Rename: the inspector's Name field takes the keyboard */

    br_project project;                /* project.bronto in the project folder */
    br_project project_edit;           /* the Project Settings dialog's copy */
    bool project_visible;
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
void ed_cmd_copy(editor *ed);
void ed_cmd_cut(editor *ed);
void ed_cmd_paste(editor *ed, const br_vec2 *at, br_entity parent);   /* at NULL: next to where it was copied */
void ed_clipboard_refresh(editor *ed);     /* scene text on the system clipboard -> ed->clipboard */
void ed_cmd_rename(editor *ed);            /* the inspector's Name field */
void ed_cmd_unparent(editor *ed);
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
void ed_panels_default(ed_panels *p);
void ed_panel_set_open(editor *ed, ed_panel panel, bool open);   /* and saves the settings */
const char *ed_panel_name(ed_panel panel);
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

/* console.c (the editor's side) */
void ed_console_poll(editor *ed);           /* every frame: captured output into ed->console */

/* build.c */
ed_target ed_host_target(void);
const char *ed_target_name(ed_target t);
bool ed_find_player(editor *ed, ed_target t, char *out, size_t size);   /* the player program for t */
void ed_project_reload(editor *ed);         /* reads project.bronto (and applies strict typing) */
bool ed_project_save(editor *ed, const br_project *p);
int  ed_check_scripts(editor *ed, int *scripts);   /* mistakes, printed to the console */
bool ed_build(editor *ed, ed_target t, char *out, size_t size);   /* out: the game made */
void ed_cmd_build(editor *ed, ed_target t, bool run);
void ed_open_builds_folder(editor *ed);
void ed_open_save_folder(editor *ed);      /* where the game keeps saved data (save.h) */
void ed_project_settings_open(editor *ed);
void ed_project_dialog(editor *ed, br_rect screen);

/* menus.c */
void ed_menus(editor *ed);                  /* every frame, after the panels: the open menu's items */
void ed_menu_entity(editor *ed, br_entity e);            /* right-clicked: open its menu */
void ed_menu_hierarchy(editor *ed);
void ed_menu_view(editor *ed, br_vec2 world);
void ed_menu_asset(editor *ed, const char *rel);         /* "" = the folder's empty space */
void ed_menu_component(editor *ed, br_component c, int field);
void ed_menu_inspector(editor *ed);
void ed_menu_console(editor *ed, int line);
bool ed_open_script_named(editor *ed, const char *name, int line);
void ed_copy_text(editor *ed, const char *text, const char *what);   /* to the system clipboard */
void ed_console_copy(editor *ed, int line);                           /* -1: every line shown */   /* "player.bs" from a message */
void ed_component_reset(editor *ed, br_entity e, br_component c, int field);   /* field -1: all */
bool ed_component_copy(editor *ed, br_entity e, br_component c);
bool ed_component_paste(editor *ed, br_entity e, br_component c);  /* c invalid: the copied kind, added */

/* panels.c */
void ed_toolbar(editor *ed, br_rect rect);
void ed_hierarchy(editor *ed, br_rect rect);
bool ed_collapsed(const editor *ed, br_entity e);
void ed_set_collapsed(editor *ed, br_entity e, bool collapsed);
void ed_inspector(editor *ed, br_rect rect);
void ed_statusbar(editor *ed, br_rect rect);
void ed_bottom_tabs(editor *ed, br_rect rect);
typedef enum ed_arrow { ED_ARROW_LEFT = 0, ED_ARROW_RIGHT, ED_ARROW_UP, ED_ARROW_DOWN } ed_arrow;
/* A small clickable square (its own little panel, so it takes the mouse
 * before the panel it sits on). Returns BR_UI_* flags; draw it afterwards,
 * on top, with ed_draw_arrow. */
int  ed_corner_button(editor *ed, const char *id, br_rect rect);
void ed_draw_arrow(br_ui *ui, br_rect rect, ed_arrow dir, bool hot);
/* A collapsed panel: a strip with an arrow and the panel's name down it.
 * True when clicked (to open it again). */
bool ed_collapsed_strip(editor *ed, br_rect rect, ed_panel panel, ed_arrow arrow);
void ed_console_panel(editor *ed, br_rect rect);
void ed_dialog(editor *ed, br_rect screen);
void ed_picker_dialog(editor *ed, br_rect screen);
void ed_picker_close(editor *ed);

#endif /* BRONTO_EDITOR_H */
