/*
 * BRONTO EDITOR - a visual scene editor built on the engine.
 *
 *   doc.c        the DOCUMENT: the scene's entities, selection, undo/redo,
 *                play/stop, load/save. No drawing, no input; unit tested.
 *   commands.c   everything the user can do (New, Save, Delete, Play...),
 *                called by buttons and keyboard shortcuts alike
 *   viewport.c   the scene view: pan, zoom, grid, picking, move/rotate/scale
 *   viewport3d.c ... in 3D: an orbit camera, the ground grid, gizmos, 3D picking
 *   panels.c     toolbar, hierarchy, inspector, status bar, dialogs
 *   assets.c     the asset browser: the project's files, previews, file
 *                operations, and dragging assets into the scene
 *   icon.c       the app icon, a little brontosaurus drawn in code
 *   settings.c   preferences (the script editor) and the Settings dialog
 *   code.c       the script editor in the Scripts tab: typing, undo, colours, mistakes
 *   debugger.c   the script debugger around it: breakpoints, stepping, variables
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
#define ED_SELECT_MAX 512

/* ======================= doc.c: the document ======================= */

/* While playing, scenes a script pushed over the edited one (push_scene). */
#define ED_OVERLAYS_MAX 8
typedef struct ed_overlay {
    br_world world;
    char path[ED_PATH_MAX];      /* relative to the project */
    bool transparent, update_below;
} ed_overlay;

typedef struct ed_doc {
    br_world world;
    br_builtin_ids ids;
    br_ids3d ids3;
    br_entity *order;            /* hierarchy order = save order */
    uint32_t count, cap;
    br_entity selected;          /* the primary selection: the inspector shows it */
    /* More entities selected along with it (ed_doc_select_toggle...). Only
     * valid while `selected` is still `multi_primary`: setting `selected`
     * directly selects that one alone. */
    br_entity multi[ED_SELECT_MAX];
    uint32_t multi_count;
    br_entity multi_primary;
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
    ed_overlay *overlays[ED_OVERLAYS_MAX];   /* scenes pushed while playing, bottom first */
    int overlay_count;
    void *context;               /* the engine, for the overlays' worlds (world.context) */
} ed_doc;

void ed_doc_init(ed_doc *d);
void ed_doc_destroy(ed_doc *d);
void ed_doc_new(ed_doc *d);                       /* empty and untitled */
void ed_doc_mark_clean(ed_doc *d);                /* the current scene is the baseline: no undo, not dirty */
bool ed_doc_load(ed_doc *d, const char *path);    /* false: d->error says why */
bool ed_doc_save(ed_doc *d, const char *path);
bool ed_doc_dirty(const ed_doc *d);

br_entity ed_doc_create(ed_doc *d, const char *name, br_vec2 pos);   /* Name + Transform, a root */
/* Prefab instances (serialize.h): the file an entity is an instance of (NULL: none),
 * the outermost instance it is part of, making them again after a prefab changes,
 * and writing an instance back to its prefab. */
const char *ed_doc_prefab_of(const ed_doc *d, br_entity e);
br_entity ed_doc_instance_root(const ed_doc *d, br_entity e);
bool ed_doc_refresh_prefabs(ed_doc *d);
bool ed_doc_rebuild_world(ed_doc *d);
struct editor;
void ed_components_reload(struct editor *ed, bool force);   /* main.c: read the game's components again (when scripts changed, or force) */       /* after the game's components (script.h) change */
bool ed_doc_apply_prefab(ed_doc *d, br_entity root);
br_entity ed_doc_create3d(ed_doc *d, const char *name, br_vec3 pos);  /* Name + Transform3D, a root */
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
/* The hierarchy's filter: words that must all match - "#enemy" a tag the entity
 * has, anything else part of its name (any case). Empty: everything matches. */
bool ed_doc_matches(const ed_doc *d, br_entity e, const char *filter);
br_entity ed_doc_pick(const ed_doc *d, br_vec2 world);               /* topmost under a point */

/* The selection: one or more entities, `selected` the primary one. */
void ed_doc_select(ed_doc *d, br_entity e);                          /* just e (NULL: nothing) */
void ed_doc_select_add(ed_doc *d, br_entity e);                      /* e too, as the primary */
void ed_doc_select_toggle(ed_doc *d, br_entity e);                   /* in or out (ctrl / shift click) */
bool ed_doc_is_selected(const ed_doc *d, br_entity e);
/* Every selected entity, in hierarchy order (`roots`: leaving out those with a
 * selected ancestor, since they move with it). Returns how many (up to max). */
uint32_t ed_doc_selection(ed_doc *d, br_entity *out, uint32_t max, bool roots);
uint32_t ed_doc_selection_count(ed_doc *d);
void ed_doc_delete_selection(ed_doc *d);                             /* each with its subtree */
uint32_t ed_doc_duplicate_selection(ed_doc *d);                      /* the copies become the selection */
char *ed_doc_copy_selection(ed_doc *d);                              /* scene text of every selected subtree */

/* Fixes the project folder (the editor's asset browser shows it). */
void ed_doc_set_root(ed_doc *d, const char *root);
/* Prefabs are ordinary scene files. Instantiating one adds copies of its
 * entities at the end of the hierarchy, moved so that their average
 * position lands on `at`, and selects the first. Returns how many were
 * added, or -1 (d->error says why). */
int  ed_doc_instantiate(ed_doc *d, const char *path, br_vec2 at);
int  ed_doc_instantiate3d(ed_doc *d, const char *path, br_vec3 at);   /* the same for 3D roots */
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
/* While playing, the scene stack a game has (push_scene / pop_scene): scenes on
 * top of the edited one, each its own world, loaded from `path` (relative to the
 * project). Stop and load_scene take them all away. The top world is the one
 * that runs (and the ones under it, while each above lets them: update_below);
 * the viewport draws from the lowest one still showing (transparent). */
bool ed_doc_push_scene(ed_doc *d, const char *path, bool transparent, bool update_below);
bool ed_doc_pop_scene(ed_doc *d);
void ed_doc_clear_overlays(ed_doc *d);
br_world *ed_doc_top_world(ed_doc *d);           /* the top of the stack (the edited scene's world when none) */
/* Each world on the stack, bottom (the edited scene, index 0) to top. */
br_world *ed_doc_stack_world(ed_doc *d, int index);
int ed_doc_stack_depth(const ed_doc *d);         /* 1 + overlays */

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

typedef enum ed_drag {
    ED_DRAG_NONE = 0, ED_DRAG_PAN, ED_DRAG_MOVE, ED_DRAG_ROTATE, ED_DRAG_SCALE,
    ED_DRAG_ORBIT, ED_DRAG_MOVE3, ED_DRAG_ROTATE3, ED_DRAG_SCALE3,         /* the 3D view */
    ED_DRAG_BOX                                                           /* a box around things, to select them */
} ed_drag;
typedef enum ed_gizmo { ED_GIZMO_MOVE = 0, ED_GIZMO_ROTATE, ED_GIZMO_SCALE } ed_gizmo;

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
    /* While playing a scene with a Camera, the view shows what the game shows:
     * the world through the game's camera (inside the game-screen frame), the
     * camera's ui layers on the frame itself. ed_view_to_screen / to_world then
     * go through the game's camera too, so picking and handles follow it. */
    bool use_game_camera;
    br_camera2d game_camera;
    int game_ui_layer;

    /* The 3D view (viewport3d.c): one view, switched per scene (a scene with
     * 3D entities opens in 3D). */
    bool three_d;
    bool through_camera;         /* look through the scene's Camera instead of the orbit camera */
    br_vec3 orbit_target;        /* the point the orbit camera turns around */
    float orbit_yaw, orbit_pitch, orbit_dist;   /* degrees (pitch > 0: looking down), units */
    ed_gizmo gizmo;
    int hot_axis;                /* the gizmo handle under the mouse: 0..2 x y z, 3 the centre, -1 none */
    int drag_axis;
    br_transform3d drag_start3;
    br_vec3 drag_from3;          /* the selection's world position when the drag began */
    br_quat drag_rot3;           /* ... its world rotation */
    float drag_param;            /* where along the axis (move), or the angle on screen (rotate) */
    br_vec3 drag_hit3;           /* free move: where the mouse met the drag plane */
    br_vec3 mouse_ground;        /* where the mouse ray meets the ground (y = 0) */
    /* Dragging several selected entities: each one's place when the drag began. */
    br_entity group[ED_SELECT_MAX];
    br_vec2 group_from[ED_SELECT_MAX];
    br_vec3 group_from3[ED_SELECT_MAX], group_scale3[ED_SELECT_MAX];
    br_quat group_rot3[ED_SELECT_MAX];
    uint32_t group_count;
    br_vec2 drag_from2;          /* the primary's world position when a 2D drag began */
    bool box_add;                /* the box adds to the selection (Shift / Ctrl held) */
    bool mouse_on_ground;
    br_r3d r3;
} ed_view;

/* What the asset browser makes of a file. */
typedef enum ed_asset_kind {
    ED_ASSET_FOLDER = 0, ED_ASSET_IMAGE, ED_ASSET_SOUND, ED_ASSET_SCENE, ED_ASSET_SCRIPT, ED_ASSET_MODEL, ED_ASSET_OTHER
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

typedef enum ed_bottom_tab { ED_TAB_FILES = 0, ED_TAB_CONSOLE, ED_TAB_DEBUG } ed_bottom_tab;

/* The script editor (code.c): the scripts open in the Scripts tab. */
#define ED_CODE_FILES 12
typedef struct ed_code_snap { char *text; int len, cursor, anchor; } ed_code_snap;
typedef struct ed_code_file {
    char path[ED_PATH_MAX * 2];        /* the full path (as breakpoints name it) */
    char name[96];                     /* on its tab */
    char *text;                        /* always 0-terminated */
    int len, cap;
    int *line_start, line_count, line_cap;   /* line k is [line_start[k], line_start[k+1] - 1) */
    int cursor, anchor;                /* byte offsets: the selection is between them */
    int want_col;                      /* up / down keep to this column (-1: the cursor's) */
    float scroll_x, scroll_y;
    bool dirty, close_asked;
    int64_t modified;                  /* the file's time when last read or saved */
    ed_code_snap *undo, *redo;
    int undo_count, redo_count;
    int group, group_end;              /* typing carries on one undo step... */
    double group_time;                 /* ... while it's quick and in one place */
    char error[256];                   /* the first mistake br_script_check found ("" : none) */
    int error_line;
    double check_at;                   /* when to check again (0: done) */
} ed_code_file;
typedef struct ed_code {
    ed_code_file *files[ED_CODE_FILES];
    int count, current;
    bool focused;                      /* the text has the keyboard */
    bool drawn, selecting;
    int scroll_to;                     /* a line to bring into view (0: none) */
    double blink, poll_at;
} ed_code;

/* The script debugger (debugger.c): the Debug tab, and the stop it is held at. */
#define ED_DEBUG_VARS 64
#define ED_DEBUG_FRAMES 16
typedef struct ed_debug {
    bool stopped;                      /* a script is held at a line (inside br_engine_frame) */
    int action;                        /* what to do next (br_debug_action), -1: still waiting */
    bool stop_after;                   /* Stop pressed while stopped: stop playing after this step */
    char file[ED_PATH_MAX * 2];        /* the script shown in the Scripts tab (full path) */
    int here;                          /* the line it is stopped at, in the frame shown... */
    char here_file[ED_PATH_MAX * 2];   /* ... in this script (full path) */
    int frame;                         /* the call-stack frame shown */
    char reason[16];
    br_entity entity;
    struct { char fn[40]; char file[ED_PATH_MAX * 2]; int line; } frames[ED_DEBUG_FRAMES];
    int nframes;
    br_debug_var locals[ED_DEBUG_VARS], globals[ED_DEBUG_VARS];
    int nlocals, nglobals;
    int stops, last_line;              /* (for the self-test) */
    int auto_frames, auto_action;      /* the self-test: answer by itself after so many frames */
    const char *auto_shot;             /* ... saving the screen then (a .bmp), if set */
} ed_debug;

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
    br_vec3 world3;                      /* ... in 3D: on the ground (or in front of the camera) */
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
    bool build_cli;                    /* running as editor --build (no window) */
    char build_out[ED_PATH_MAX];       /* --build --out: the folder the game goes in ("": the project's builds/<target>) */
    br_image badge;                    /* the icon, small, for the toolbar */
    struct { char script_editor[256]; } prefs;   /* saved in the user's settings folder */
    char settings_field[256];
    bool settings_visible;
    br_entity collapsed[ED_MAX_COLLAPSED];   /* folded rows in the hierarchy */
    int collapsed_count;
    uint32_t script_errors_seen;       /* to report new script errors while playing */
    bool show_colliders;

    ed_bottom_tab bottom_tab;          /* Files, Console or Debug under the scene view */
    ed_debug dbg;
    ed_code code;
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
    char hierarchy_filter[64];         /* the hierarchy shows only what matches (ed_doc_matches) */
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
void ed_cmd_delete(editor *ed);               /* the whole selection */
void ed_cmd_select_all(editor *ed);
void ed_cmd_copy(editor *ed);
void ed_cmd_cut(editor *ed);
void ed_cmd_paste(editor *ed, const br_vec2 *at, br_entity parent);   /* at NULL: next to where it was copied */
void ed_clipboard_refresh(editor *ed);     /* scene text on the system clipboard -> ed->clipboard */
void ed_cmd_rename(editor *ed);            /* the inspector's Name field */
void ed_cmd_unparent(editor *ed);
void ed_cmd_nudge(editor *ed, br_vec2 delta);    /* in 3D: x along the ground to the right, y towards you */
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
void ed_view_destroy(ed_view *v);
void ed_view_grab_group(editor *ed);              /* where each selected root is, as a drag begins */

/* viewport3d.c */
void ed_view_set_3d(editor *ed, bool on);
void ed_view3d_frame(editor *ed, br_rect rect);   /* ed_view_frame, in 3D */
br_camera3d ed_view3d_camera(const ed_view *v);
void ed_view3d_focus(editor *ed);                 /* Frame: orbit around the selection */
void ed_view3d_reset(ed_view *v);
br_vec3 ed_view3d_place(const editor *ed);        /* where new things go: the orbit point (snapped) */
bool ed_view3d_project(const editor *ed, br_vec3 p, br_vec2 *screen);
br_entity ed_view3d_pick(const editor *ed, br_vec2 screen);
/* New 3D things: a Mesh of a shape (cube, sphere, ...), a character (a capsule with a
 * capsule Collider3D, for move_and_slide()), a Camera3D, a Light or nothing. */
typedef enum ed_new3d { ED_NEW_CUBE = 0, ED_NEW_SPHERE, ED_NEW_PLANE, ED_NEW_CYLINDER, ED_NEW_CONE, ED_NEW_CAPSULE, ED_NEW_CHARACTER, ED_NEW_PARTICLES, ED_NEW_CAMERA, ED_NEW_LIGHT, ED_NEW_EMPTY, ED_NEW_COUNT } ed_new3d;
const char *ed_new3d_name(ed_new3d kind);
br_entity ed_new3d_at(editor *ed, ed_new3d kind, br_vec3 at);

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
/* ... in the 3D view: a model becomes a Mesh entity, an image the texture of
 * the mesh `onto`, a script is attached to `onto` (or a new entity), a scene
 * a prefab copy. */
bool ed_assets_drop3d(editor *ed, const char *rel, br_vec3 at, br_entity onto);
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
bool ed_build(editor *ed, ed_target t, char *out, size_t size);
bool ed_target_from_name(const char *name, ed_target *t);   /* "linux", "macos", "windows", "web", "host" */   /* out: the game made */
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
/* code.c */
void ed_code_init(editor *ed);
void ed_code_destroy(editor *ed);
void ed_code_poll(editor *ed);
/* Opens a script (project-relative or a full path) in the Scripts tab, at a line (0: where it was). */
bool ed_code_open(editor *ed, const char *path, int line);
int  ed_code_find(editor *ed, const char *path);              /* its index, -1: not open */
ed_code_file *ed_code_current(editor *ed);
bool ed_code_save(editor *ed, int index);
int  ed_code_save_all(editor *ed);                          /* the ones with changes; how many */
int  ed_code_dirty_count(const editor *ed);
bool ed_code_close(editor *ed, int index, bool force);
void ed_code_close_all(editor *ed);
bool ed_code_has_keyboard(const editor *ed);
int  ed_code_line_of(const ed_code_file *f, int pos);
void ed_code_view(editor *ed, br_rect rect, int here);
/* debugger.c */
void ed_debug_init(editor *ed);
void ed_debug_destroy(editor *ed);
void ed_debug_panel(editor *ed, br_rect rect);
bool ed_debug_show_file(editor *ed, const char *path, int line);
bool ed_debug_toggle_breakpoint(editor *ed, int line);
void ed_debug_after_frame(editor *ed);
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
