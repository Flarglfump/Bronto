/*
 * BRONTO EDITOR - entry point: engine setup, window layout and shortcuts.
 *
 *   editor [scene file]        open a scene (or start with a sample one)
 *   editor [project folder]    open a project (its main.scene, if it has one)
 *   editor --headless          scripted self-test with screenshots
 *   editor --build <target> [project folder] [--out folder]
 *                              the Build menu without a window: pack the project (default: the
 *                              current folder) into a game for linux, macos, windows, web or host
 *                              (this computer), into its builds/<target>/ or the --out folder.
 *                              Exits 0 when built; mistakes in scripts and other problems go to
 *                              stderr and exit 1. (make game GAME=... runs it.)
 *   editor --write-icon f.png [size]   save the app icon (the VS Code extension uses it)
 *
 * Shortcuts (Ctrl, or Cmd on a Mac):
 *   Ctrl+Shift+O load project   Ctrl+N new   Ctrl+O open   Ctrl+S save   Ctrl+Z undo   Ctrl+Y / Ctrl+Shift+Z redo
 *   Ctrl+D duplicate   Delete remove   F5 / Ctrl+P play / stop   F frame selection
 *   Ctrl+B build the game for this computer   Ctrl+Shift+B build and run it
 *   Ctrl+C / Ctrl+X / Ctrl+V copy, cut, paste entities (as scene text on the
 *   system clipboard, so they paste into another editor too)   F2 rename
 *   G grid   arrows nudge (Shift: one grid step)   Esc deselect / stop
 *   Right-click anything for a menu of what can be done with it (menus.c).
 *
 * stdout and stderr are captured into the Console tab (and still reach the
 * terminal the editor was started from).
 *
 * The editor is one engine scene. Its own UI is drawn with br_ui; the
 * edited scene is a separate ECS world owned by the document (doc.c).
 */
#include "editor.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOOLBAR_H 28.0f
#define STATUS_H 20.0f
#define TABS_H 22.0f
#define STRIP_W 20.0f                 /* a collapsed side panel */
#define VIEW_MIN_W 240.0f             /* the scene view keeps at least this much */
#define VIEW_MIN_H 120.0f
#define FILES_MIN_W 220.0f            /* the file browser, beside the preview */
/* The least each panel can be while open; dragging its edge below half of
 * this collapses it. */
static const float k_panel_min[ED_PANEL_COUNT] = { 150, 220, 90, 150 };

static editor ED;
static const char *g_open_path;

/* ---- layout ---------------------------------------------------------------- */

typedef struct layout {
    br_rect screen, toolbar, hierarchy, view, tabs, assets, preview, console, inspector, status;
    br_rect edge[ED_PANEL_COUNT];     /* where each panel's edge is grabbed (w 0: none now) */
    br_rect button[ED_PANEL_COUNT];   /* its collapse arrow */
    float max[ED_PANEL_COUNT];        /* the most it can be in this window */
} layout;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static layout make_layout(const editor *ed) {
    const ed_panels *p = &ed->panels;
    layout l;
    float w = (float)ed->engine->framebuffer.width, h = (float)ed->engine->framebuffer.height, mid = h - TOOLBAR_H - STATUS_H;
    float left, right, centre_w, bottom_h, view_h, preview_w, bottom_y, over;
    bool files = ed->bottom_tab == ED_TAB_FILES && p->open[ED_PANEL_BOTTOM];
    memset(&l, 0, sizeof(l));
    /* the side panels, then the scene view gets what is left (at least VIEW_MIN_W) */
    left = p->open[ED_PANEL_SCENE] ? fmaxf(p->size[ED_PANEL_SCENE], k_panel_min[ED_PANEL_SCENE]) : STRIP_W;
    right = p->open[ED_PANEL_INSPECTOR] ? fmaxf(p->size[ED_PANEL_INSPECTOR], k_panel_min[ED_PANEL_INSPECTOR]) : STRIP_W;
    over = left + right + VIEW_MIN_W - w;
    if (over > 0 && p->open[ED_PANEL_INSPECTOR]) { float cut = fminf(over, right - k_panel_min[ED_PANEL_INSPECTOR]); right -= cut; over -= cut; }
    if (over > 0 && p->open[ED_PANEL_SCENE]) { float cut = fminf(over, left - k_panel_min[ED_PANEL_SCENE]); left -= cut; }
    left = floorf(left);
    right = floorf(right);
    centre_w = w - left - right;
    l.max[ED_PANEL_SCENE] = fmaxf(k_panel_min[ED_PANEL_SCENE], w - right - VIEW_MIN_W);
    l.max[ED_PANEL_INSPECTOR] = fmaxf(k_panel_min[ED_PANEL_INSPECTOR], w - left - VIEW_MIN_W);
    /* under the view: the tabs, and Files / Console when open */
    l.max[ED_PANEL_BOTTOM] = fmaxf(k_panel_min[ED_PANEL_BOTTOM], mid - TABS_H - VIEW_MIN_H);
    bottom_h = p->open[ED_PANEL_BOTTOM] ? floorf(clampf(p->size[ED_PANEL_BOTTOM], fminf(k_panel_min[ED_PANEL_BOTTOM], mid - TABS_H - 40),
                                                        l.max[ED_PANEL_BOTTOM])) : 0;
    view_h = mid - bottom_h - TABS_H;
    bottom_y = TOOLBAR_H + view_h + TABS_H;
    l.max[ED_PANEL_PREVIEW] = fmaxf(k_panel_min[ED_PANEL_PREVIEW], centre_w - FILES_MIN_W);
    preview_w = p->open[ED_PANEL_PREVIEW] ? floorf(clampf(p->size[ED_PANEL_PREVIEW], fminf(k_panel_min[ED_PANEL_PREVIEW], centre_w / 2),
                                                          l.max[ED_PANEL_PREVIEW])) : STRIP_W;
    l.screen = br_rect_make(0, 0, w, h);
    l.toolbar = br_rect_make(0, 0, w, TOOLBAR_H);
    l.hierarchy = br_rect_make(0, TOOLBAR_H, left, mid);
    l.inspector = br_rect_make(w - right, TOOLBAR_H, right, mid);
    l.view = br_rect_make(left + 1, TOOLBAR_H + 1, centre_w - 2, view_h - 2);
    l.tabs = br_rect_make(left, TOOLBAR_H + view_h, centre_w, TABS_H);
    l.assets = br_rect_make(left, bottom_y, centre_w - preview_w - 1, bottom_h);
    l.preview = br_rect_make(left + centre_w - preview_w, bottom_y, preview_w, bottom_h);
    l.console = br_rect_make(left, bottom_y, centre_w, bottom_h);
    l.status = br_rect_make(0, h - STATUS_H, w, STATUS_H);
    /* the edges to drag (mostly over the scene view, clear of the panels' scroll bars) */
    l.edge[ED_PANEL_SCENE] = br_rect_make(left - 1, TOOLBAR_H, 5, mid);
    l.edge[ED_PANEL_INSPECTOR] = br_rect_make(w - right - 4, TOOLBAR_H, 5, mid);
    l.edge[ED_PANEL_BOTTOM] = br_rect_make(left, l.tabs.y - 4, centre_w, 5);
    if (files) l.edge[ED_PANEL_PREVIEW] = br_rect_make(l.preview.x - 1, bottom_y, 5, bottom_h);
    /* the arrows that collapse them (or open them: the bottom one stays on the tabs) */
    l.button[ED_PANEL_SCENE] = br_rect_make(left - 22, TOOLBAR_H + 4, 16, 16);
    l.button[ED_PANEL_INSPECTOR] = br_rect_make(w - 22, TOOLBAR_H + 4, 16, 16);
    l.button[ED_PANEL_BOTTOM] = br_rect_make(left + centre_w - 22, l.tabs.y + 3, 16, 16);
    if (files) l.button[ED_PANEL_PREVIEW] = br_rect_make(l.preview.x + preview_w - 22, bottom_y + 4, 16, 16);
    return l;
}

/* Where a panel's edge is now, along the axis it moves on. */
static float edge_at(const layout *l, ed_panel i) {
    switch (i) {
    case ED_PANEL_SCENE: return l->hierarchy.w;
    case ED_PANEL_INSPECTOR: return l->inspector.x;
    case ED_PANEL_BOTTOM: return l->tabs.y;
    default: return l->preview.x;
    }
}
/* The size a panel has with its edge at `at`. */
static float size_for_edge(const layout *l, ed_panel i, float at) {
    switch (i) {
    case ED_PANEL_SCENE: return at;
    case ED_PANEL_INSPECTOR: return l->screen.w - at;
    case ED_PANEL_BOTTOM: return l->status.y - TABS_H - at;
    default: return l->tabs.x + l->tabs.w - at;
    }
}

/* Dragging the panels' edges, double-clicking them (collapse / open), and
 * the collapse arrows. Runs before the panels, so it gets the mouse first. */
static void panel_chrome(editor *ed, const layout *l) {
    const br_input *in = &ed->engine->input;
    int i;
    ed->split_hot = -1;
    ed->chrome_clicked = false;
    for (i = 0; i < ED_PANEL_COUNT; ++i) {
        ed_panel pnl = (ed_panel)i;
        char id[16];
        bool horizontal = pnl == ED_PANEL_BOTTOM;
        float mouse = horizontal ? (float)in->mouse_y : (float)in->mouse_x;
        int f;
        if (l->edge[i].w <= 0) continue;
        snprintf(id, sizeof(id), "edge_%d", i);
        f = ed_corner_button(ed, id, l->edge[i]);
        if (f & BR_UI_PRESSED) {
            ed->split_drag = i;
            ed->split_grab = mouse - edge_at(l, pnl);
            ed->split_start = ed->panels.size[i];
        }
        if (ed->split_drag == i && (f & BR_UI_HELD)) {
            float size = size_for_edge(l, pnl, mouse - ed->split_grab), least = k_panel_min[i];
            if (fabsf(mouse - ed->split_grab - edge_at(l, pnl)) >= 1) {
                if (size < least * 0.5f) {                     /* most of the way closed: collapse */
                    ed->panels.open[i] = false;
                    ed->panels.size[i] = fmaxf(ed->split_start, least);
                } else {
                    ed->panels.open[i] = true;
                    ed->panels.size[i] = floorf(clampf(size, least, l->max[i]));
                }
            }
        }
        if (f & BR_UI_CLICKED) ed->chrome_clicked = true;
        if (f & BR_UI_DOUBLE_CLICKED) {
            ed->panels.size[i] = fmaxf(ed->split_start, k_panel_min[i]);
            ed->panels.open[i] = !ed->panels.open[i];
        }
        if (f & BR_UI_RELEASED) {
            ed->split_drag = -1;
            ed_prefs_save(ed);
        }
        if ((f & BR_UI_HOVERED) || ed->split_drag == i) ed->split_hot = i;
    }
    for (i = 0; i < ED_PANEL_COUNT; ++i) {
        char id[16];
        if (l->button[i].w <= 0 || (!ed->panels.open[i] && i != ED_PANEL_BOTTOM)) continue;   /* (a collapsed side panel has its strip) */
        snprintf(id, sizeof(id), "collapse_%d", i);
        if (ed_corner_button(ed, id, l->button[i]) & BR_UI_CLICKED) {
            ed_panel_set_open(ed, (ed_panel)i, !ed->panels.open[i]);
            ed->chrome_clicked = true;
        }
    }
}

/* Drawn last, on top of the panels: the arrows, and the edge under the mouse. */
static void panel_chrome_draw(editor *ed, const layout *l) {
    static const ed_arrow k_close[ED_PANEL_COUNT] = { ED_ARROW_LEFT, ED_ARROW_RIGHT, ED_ARROW_DOWN, ED_ARROW_RIGHT };
    br_ui *ui = &ed->ui;
    br_vec2 m = br_v2((float)ed->engine->input.mouse_x, (float)ed->engine->input.mouse_y);
    int i;
    for (i = 0; i < ED_PANEL_COUNT; ++i) {
        br_rect b = l->button[i];
        bool hot = !ui->disabled && m.x >= b.x && m.y >= b.y && m.x < b.x + b.w && m.y < b.y + b.h;
        if (b.w <= 0 || (!ed->panels.open[i] && i != ED_PANEL_BOTTOM)) continue;
        ed_draw_arrow(ui, b, ed->panels.open[i] ? k_close[i] : ED_ARROW_UP, hot);
    }
    if (ed->split_hot >= 0 && !ui->disabled) {
        br_rect e = l->edge[ed->split_hot];
        if (ed->split_hot == ED_PANEL_BOTTOM) br_fill_rect(ui->r, br_rect_make(e.x, l->tabs.y - 1, e.w, 2), ui->style.accent);
        else br_fill_rect(ui->r, br_rect_make(edge_at(l, (ed_panel)ed->split_hot) - 1, e.y, 2, e.h), ui->style.accent);
    }
}

/* ---- shortcuts ---------------------------------------------------------------- */

static void shortcuts(editor *ed) {
    const br_input *in = &ed->engine->input;
    bool cmd = (in->mods & (BR_MOD_CONTROL | BR_MOD_SUPER)) != 0, shift = (in->mods & BR_MOD_SHIFT) != 0;
    float step;
    if (br_ui_wants_keyboard(&ed->ui) || br_ui_menu_active(&ed->ui) ||                /* typing, a menu, or a dialog */
        ed->pending != ED_PENDING_NONE || ed->picker.open || ed->settings_visible || ed->project_visible) return;
    if (ed_code_has_keyboard(ed) && !ed->doc.playing) {   /* the script editor has the keyboard: only Play */
        if (br_key_pressed(in, BR_KEY_F5) || (cmd && br_key_pressed(in, BR_KEY_P))) ed_cmd_play(ed);
        return;
    }
    if (ed->doc.playing) {                         /* the game has the keyboard: only Stop */
        if (br_key_pressed(in, BR_KEY_F5) || br_key_pressed(in, BR_KEY_ESCAPE) || (cmd && br_key_pressed(in, BR_KEY_P))) ed_cmd_play(ed);
        return;
    }
    if (cmd && br_key_pressed(in, BR_KEY_N)) ed_cmd_new(ed);
    if (cmd && br_key_pressed(in, BR_KEY_B)) ed_cmd_build(ed, ed_host_target(), shift);
    if (cmd && br_key_pressed(in, BR_KEY_O)) { if (shift) ed_cmd_load_project(ed); else ed_cmd_open(ed); }
    if (cmd && br_key_pressed(in, BR_KEY_S)) ed_cmd_save(ed);
    if (cmd && br_key_pressed(in, BR_KEY_Z)) { if (shift) ed_cmd_redo(ed); else ed_cmd_undo(ed); }
    if (cmd && br_key_pressed(in, BR_KEY_Y)) ed_cmd_redo(ed);
    if (cmd && br_key_pressed(in, BR_KEY_D)) ed_cmd_duplicate(ed);
    if (cmd && br_key_pressed(in, BR_KEY_A)) ed_cmd_select_all(ed);
    if (cmd && br_key_pressed(in, BR_KEY_C)) {
        layout l = make_layout(ed);                 /* over the console: its selected line */
        br_vec2 m = br_v2(in->mouse_x, in->mouse_y);
        if (ed->bottom_tab == ED_TAB_CONSOLE && ed->console_selected >= 0 && m.x >= l.console.x && m.y >= l.console.y &&
            m.x < l.console.x + l.console.w && m.y < l.console.y + l.console.h) ed_console_copy(ed, ed->console_selected);
        else ed_cmd_copy(ed);
    }
    if (cmd && br_key_pressed(in, BR_KEY_X)) ed_cmd_cut(ed);
    if (cmd && br_key_pressed(in, BR_KEY_V)) {                 /* next to the selection, as its sibling */
        br_entity sel = ed->doc.selected;
        ed_cmd_paste(ed, NULL, br_entity_alive(&ed->doc.world, sel) ? br_parent_of(&ed->doc.world, sel) : BR_ENTITY_NULL);
    }
    if (br_key_pressed(in, BR_KEY_F2)) ed_cmd_rename(ed);
    if (br_key_pressed(in, BR_KEY_F5) || (cmd && br_key_pressed(in, BR_KEY_P))) ed_cmd_play(ed);
    if (br_key_pressed(in, BR_KEY_DELETE) || (!cmd && br_key_pressed(in, BR_KEY_BACKSPACE))) ed_cmd_delete(ed);
    if (!cmd && br_key_pressed(in, BR_KEY_F)) ed_cmd_frame(ed);
    if (!cmd && br_key_pressed(in, BR_KEY_G)) ed->view.grid = !ed->view.grid;
    if (br_key_pressed(in, BR_KEY_ESCAPE)) {
        if (ed->doc.playing) ed_cmd_play(ed);
        else ed->doc.selected = BR_ENTITY_NULL;
    }
    step = shift ? ed->view.grid_size : 1.0f;
    if (br_key_repeated(in, BR_KEY_LEFT))  ed_cmd_nudge(ed, br_v2(-step, 0));
    if (br_key_repeated(in, BR_KEY_RIGHT)) ed_cmd_nudge(ed, br_v2(step, 0));
    if (br_key_repeated(in, BR_KEY_UP))    ed_cmd_nudge(ed, br_v2(0, -step));
    if (br_key_repeated(in, BR_KEY_DOWN))  ed_cmd_nudge(ed, br_v2(0, step));
}

static void update_title(editor *ed) {
    static char last[ED_PATH_MAX + 32];
    char title[ED_PATH_MAX + 32];
    snprintf(title, sizeof(title), "%s%s - Bronto Editor", ed->doc.path[0] ? ed->doc.path : "untitled",
             ed_doc_dirty(&ed->doc) ? " *" : "");
    if (strcmp(title, last) != 0) {
        br_window_set_title(ed->engine, title);
        snprintf(last, sizeof(last), "%s", title);
    }
}

/* ---- the editor scene ---------------------------------------------------------- */

static bool ui_clip_set(void *engine, const char *text) { return br_clipboard_set((br_engine *)engine, text); }
static char *ui_clip_get(void *engine) { return br_clipboard_get((br_engine *)engine); }

static bool editor_enter(br_scene *s) {
    editor *ed = &ED;
    ed->engine = s->engine;
    br_ui_init(&ed->ui);
    ed_view_init(&ed->view);
    {   /* text fields copy and paste with the system clipboard */
        br_ui_clipboard cb;
        cb.set = ui_clip_set;
        cb.get = ui_clip_get;
        cb.user = ed->engine;
        br_ui_set_clipboard(&ed->ui, &cb);
    }
    ed->show_colliders = true;
    ed->console_show[ED_LEVEL_LOG] = ed->console_show[ED_LEVEL_WARN] = ed->console_show[ED_LEVEL_ERROR] = true;
    ed->console_selected = -1;
    if (!ed->console_capturing)
        ed_console_add(&ed->console, ED_CON_INFO, "Output capture isn't available here: only script print() shows up.", 0);
    ed_panels_default(&ed->panels);
    ed->split_drag = ed->split_hot = -1;
    ed_prefs_load(ed);
    ed_doc_init(&ed->doc);
    ed_code_init(ed);                                /* the Scripts tab's editor (code.c) */
    ed_debug_init(ed);                               /* breakpoints stop scripts here (debugger.c) */
    ed->doc.world.context = ed->engine;              /* Sound components play while playing */
    ed->doc.context = ed->engine;                    /* ... and in scenes pushed over it (push_scene) */
    {   /* the project: the scene's (see br_assets_find_root), or the folder the editor started in */
        char root[ED_PATH_MAX] = "";
        if (g_open_path && !br_fs_is_dir(g_open_path)) br_assets_find_root(g_open_path, root, sizeof(root));
        ed_assets_init(ed, root);
    }
    ed_components_reload(ed, true);                  /* (before a scene is opened: it may use them) */
    if (g_open_path && br_fs_is_dir(g_open_path)) {
        if (!ed_open_project(ed, g_open_path)) ed_sample_scene(&ed->doc);
    } else if (g_open_path) {
        snprintf(ed->path_field, sizeof(ed->path_field), "%s", g_open_path);
        if (!ed_doc_load(&ed->doc, g_open_path)) {
            ed_status(ed, "Couldn't open %s: %s. Starting with a sample scene", g_open_path, ed->doc.error);
            ed_sample_scene(&ed->doc);
        } else {
            ed_status(ed, "Opened %s", g_open_path);
        }
    } else {
        ed_sample_scene(&ed->doc);
        ed_status(ed, "Welcome! This is a sample scene: click things, drag them, press Play");
    }
    ed_view_set_3d(ed, br_world_has_3d(&ed->doc.world));
    ed_cmd_frame(ed);
    return true;
}

static void editor_exit(br_scene *s) {
    BR_UNUSED(s);
    ed_debug_destroy(&ED);
    ed_code_destroy(&ED);
    ed_assets_destroy(&ED);
    ed_picker_close(&ED);
    ed_doc_destroy(&ED.doc);
    ed_view_destroy(&ED.view);
    br_image_destroy(&ED.badge);
    br_free(ED.clipboard);
    ED.clipboard = NULL;
}

static void editor_fixed(br_scene *s, float dt) {
    BR_UNUSED(s);
    ed_doc_step(&ED.doc, dt);                       /* the scene's systems run only while playing */
}

/* The game's own components (declared in its scripts, script.h): read when a
 * project opens, and again when a script changes; the scene's world is made again
 * with them if they changed. */
static int64_t scripts_stamp(const char *dir, int depth) {
    br_dir_list l;
    int64_t sum = 0;
    int i;
    if (depth > 8 || !br_fs_list(dir, false, &l)) return 0;
    for (i = 0; i < l.count; ++i) {
        const br_dir_entry *e = &l.entries[i];
        char full[ED_PATH_MAX];
        if (e->name[0] == '.' || strcmp(e->name, "builds") == 0) continue;
        br_path_join(full, sizeof(full), dir, e->name);
        if (e->is_dir) sum += scripts_stamp(full, depth + 1);
        else if (br_path_has_ext(e->name, ".bs")) sum += e->modified * 31 + (int64_t)e->size;
    }
    br_dir_list_free(&l);
    return sum;
}

void ed_components_reload(editor *ed, bool force) {
    static int64_t stamp;
    static double last;
    char err[300];
    int64_t now;
    uint32_t before = br_script_components_signature();
    const char *root = br_assets_root();
    if (!force && (ed->doc.playing || ed->engine->time - last < 2.0)) return;
    last = ed->engine ? ed->engine->time : 0;
    now = root[0] ? scripts_stamp(root, 0) : 0;
    if (!force && now == stamp) return;
    stamp = now;
    if (br_script_components_from_project(root, err, sizeof(err)) < 0) ed_status(ed, "Component: %s", err);
    if (br_script_components_signature() != before && !ed->doc.playing) {
        ed_doc_rebuild_world(&ed->doc);
        if (!force) ed_status(ed, "The game's components changed (%d of them): the scene has them now", br_script_component_count());
    }
}

/* Prefab files the scene's instances come from: when one changes on disk (saved in
 * another editor, or made again by a tool), the instances are made again. */
static void prefab_watch(editor *ed) {
    static struct { char file[128]; int64_t when; } seen[32];
    static int seen_count;
    static double last;
    br_component pc = br_component_find(&ed->doc.world, "Prefab");
    const br_component_pool *pool;
    bool changed = false;
    uint32_t i;
    int k, n = 0;
    struct { char file[128]; int64_t when; } now[32];
    if (ed->doc.playing || pc == BR_COMPONENT_INVALID || ed->engine->time - last < 1.0) return;
    last = ed->engine->time;
    pool = &ed->doc.world.pools[pc];
    for (i = 0; i < pool->count && n < 32; ++i) {
        const br_prefab *p = (const br_prefab *)(pool->data + (size_t)i * pool->stride);
        char full[ED_PATH_MAX * 2];
        br_dir_entry st;
        bool dup = false;
        for (k = 0; k < n; ++k) if (strcmp(now[k].file, p->file) == 0) dup = true;
        if (dup || !p->file[0]) continue;
        snprintf(full, sizeof(full), "%s%s", br_assets_root(), p->file);
        snprintf(now[n].file, sizeof(now[n].file), "%s", p->file);
        now[n].when = br_fs_stat(full, &st) ? st.modified : 0;
        for (k = 0; k < seen_count; ++k)
            if (strcmp(seen[k].file, now[n].file) == 0 && seen[k].when != now[n].when) changed = true;
        ++n;
    }
    memcpy(seen, now, sizeof(now[0]) * (size_t)n);
    seen_count = n;
    if (changed && ed_doc_refresh_prefabs(&ed->doc)) ed_status(ed, "A prefab changed: its instances here were made again (keeping this scene's changes)");
}

static void editor_update(br_scene *s, float dt) {
    editor *ed = &ED;
    BR_UNUSED(s);
    BR_UNUSED(dt);
    /* Between interactions, record an undo step if anything changed. */
    if (ed->ui.active == 0 && !br_ui_wants_keyboard(&ed->ui) && ed->view.drag == ED_DRAG_NONE)
        ed_doc_checkpoint(&ed->doc);
    ed_console_poll(ed);
    ed_debug_after_frame(ed);
    prefab_watch(ed);
    ed_code_poll(ed);
    ed_components_reload(ed, false);
    shortcuts(ed);
    update_title(ed);
    if (ed->doc.playing) {                          /* what the game's scripts asked for, in each scene on the stack */
        char next[ED_PATH_MAX], full[ED_PATH_MAX * 2];
        int i;
        bool transparent, below;
        for (i = ed_doc_stack_depth(&ed->doc) - 1; i >= 0 && ed->doc.playing; --i) {
            br_world *w = ed_doc_stack_world(&ed->doc, i);
            if (!w) continue;
            if (br_script_quit_request(w)) {
                ed_cmd_play(ed);
                ed_status(ed, "The game called quit(): stopped playing");
                break;
            }
            if (br_script_scene_request(w, next, sizeof(next))) {
                snprintf(full, sizeof(full), "%s%s", br_assets_root(), next);
                if (ed_doc_switch_scene(&ed->doc, full)) {
                    ed_view_set_3d(ed, br_world_has_3d(&ed->doc.world));
                    ed_status(ed, "load_scene(\"%s\") - Stop goes back to the scene you were editing", next);
                }
                else ed_status(ed, "load_scene(\"%s\"): %s", next, ed->doc.error);
                break;
            }
            if (br_script_pop_request(w)) {
                if (ed_doc_pop_scene(&ed->doc)) ed_status(ed, "pop_scene()");
                break;
            }
            if (br_script_push_request(w, next, sizeof(next), &transparent, &below)) {
                if (ed_doc_push_scene(&ed->doc, next, transparent, below)) ed_status(ed, "push_scene(\"%s\") - Stop goes back to the scene you were editing", next);
                else ed_status(ed, "push_scene(\"%s\"): %s", next, ed->doc.error);
                break;
            }
        }
    }
    if (ed->doc.playing) {                          /* a script broke, in any scene on the stack */
        uint32_t errors = 0;
        int i;
        const char *last = "";
        for (i = 0; i < ed_doc_stack_depth(&ed->doc); ++i) {
            br_world *w = ed_doc_stack_world(&ed->doc, i);
            errors += br_script_error_count(w);
            if (br_script_error_count(w)) last = br_script_last_error(w);
        }
        if (errors > ed->script_errors_seen) ed_status(ed, "Script error: %s", last);
        ed->script_errors_seen = errors;
    }
}

/* All drawing (and the widgets' input) happens here, after the frame is cleared. */
static void editor_render(br_scene *s) {
    editor *ed = &ED;
    br_engine *e = s->engine;
    layout l = make_layout(ed);
    br_ui_begin(&ed->ui, &e->renderer, &e->input, e->time);
    bool held = ed->dbg.stopped;                            /* a script is stopped mid-step: look, don't touch */
    br_ui_set_disabled(&ed->ui, held || ed->pending != ED_PENDING_NONE || ed->picker.open || ed->settings_visible || ed->project_visible);
    panel_chrome(ed, &l);                                   /* edges and arrows first: they get the mouse */
    l = make_layout(ed);
    ed_view_frame(ed, l.view);
    ed_toolbar(ed, l.toolbar);
    if (ed->panels.open[ED_PANEL_SCENE]) ed_hierarchy(ed, l.hierarchy);
    else if (ed_collapsed_strip(ed, l.hierarchy, ED_PANEL_SCENE, ED_ARROW_RIGHT)) ed_panel_set_open(ed, ED_PANEL_SCENE, true);
    if (ed->panels.open[ED_PANEL_INSPECTOR]) ed_inspector(ed, l.inspector);
    else if (ed_collapsed_strip(ed, l.inspector, ED_PANEL_INSPECTOR, ED_ARROW_LEFT)) ed_panel_set_open(ed, ED_PANEL_INSPECTOR, true);
    if (held) br_ui_set_disabled(&ed->ui, false);
    ed_bottom_tabs(ed, l.tabs);
    if (!ed->panels.open[ED_PANEL_BOTTOM]) {
        /* only the tabs: clicking one opens it again */
    } else if (ed->bottom_tab == ED_TAB_DEBUG) {
        ed_debug_panel(ed, l.console);
    } else if (ed->bottom_tab == ED_TAB_CONSOLE) {
        ed_console_panel(ed, l.console);
    } else {
        ed_assets_panel(ed, l.assets);
        if (ed->panels.open[ED_PANEL_PREVIEW]) ed_assets_preview(ed, l.preview);
        else if (ed_collapsed_strip(ed, l.preview, ED_PANEL_PREVIEW, ED_ARROW_LEFT)) ed_panel_set_open(ed, ED_PANEL_PREVIEW, true);
    }
    if (held) br_ui_set_disabled(&ed->ui, true);
    ed_statusbar(ed, l.status);
    panel_chrome_draw(ed, &l);
    if (held || ed->pending != ED_PENDING_NONE || ed->picker.open || ed->settings_visible || ed->project_visible) br_ui_menu_close(&ed->ui);
    else ed_menus(ed);                                      /* the open right-click menu (drawn on top) */
    ed_dialog(ed, l.screen);
    ed_picker_dialog(ed, l.screen);
    ed_settings_dialog(ed, l.screen);
    ed_project_dialog(ed, l.screen);
    br_ui_set_disabled(&ed->ui, false);
    br_ui_end(&ed->ui);
}

static bool editor_event(br_scene *s, const br_event *ev) {
    BR_UNUSED(s);
    if (ev->type == BR_EVENT_WINDOW_CLOSE_REQUESTED && ED.dbg.stopped) {          /* let the script go on, stop playing, then ask */
        ED.dbg.action = BR_DEBUG_DETACH;
        ED.dbg.stop_after = true;
        return true;
    }
    if (ev->type == BR_EVENT_WINDOW_CLOSE_REQUESTED) return !ed_cmd_quit(&ED);   /* consumed = stay open */
    if (ev->type == BR_EVENT_WINDOW_SCALE_CHANGED) {
        ED.engine->window.pixel_scale = (int)(br_window_scale(ED.engine) + 0.5f);
        br_engine_sync_window(ED.engine);
    }
    return false;
}

static const br_scene_desc k_editor = {
    "editor", 0, false, false,
    editor_enter, editor_exit, NULL, NULL, editor_fixed, editor_update, editor_render, editor_event
};

/* ---- headless self-test ---------------------------------------------------------- */

static void inject_click(float x, float y) {
    br_event m = br_event_mouse_move(x, y, 1, 1);
    br_event d = br_event_mouse_button(BR_EVENT_MOUSE_BUTTON_DOWN, BR_MOUSE_LEFT, x, y);
    br_event u = br_event_mouse_button(BR_EVENT_MOUSE_BUTTON_UP, BR_MOUSE_LEFT, x, y);
    br_headless_inject(&m);
    br_headless_inject(&d);
    br_headless_inject(&u);
}

static void frames(br_engine *e, int n) { while (n-- > 0) br_engine_frame(e); }

#define HIER_ROW_Y(i) (TOOLBAR_H + 6 + 19 * 4 + 5 + 3 + 19 * (float)(i) + 8)   /* heading, two button rows, the filter */

/* Press at a, move there in steps, release at b. */
static void inject_drag(br_engine *e, br_vec2 a, br_vec2 b) {
    int i;
    br_event m = br_event_mouse_move(a.x, a.y, 1, 1);
    br_event d = br_event_mouse_button(BR_EVENT_MOUSE_BUTTON_DOWN, BR_MOUSE_LEFT, a.x, a.y);
    br_event u = br_event_mouse_button(BR_EVENT_MOUSE_BUTTON_UP, BR_MOUSE_LEFT, b.x, b.y);
    br_headless_inject(&m);
    br_headless_inject(&d);
    frames(e, 1);
    for (i = 1; i <= 6; ++i) {
        br_vec2 q = br_v2_lerp(a, b, (float)i / 6.0f);
        m = br_event_mouse_move(q.x, q.y, 1, 1);
        br_headless_inject(&m);
        frames(e, 1);
    }
    br_headless_inject(&u);
    frames(e, 2);
}

static void inject_move(float x, float y) { br_event m = br_event_mouse_move(x, y, 1, 1); br_headless_inject(&m); }
static void inject_button(bool down, float x, float y) {
    br_event b = br_event_mouse_button(down ? BR_EVENT_MOUSE_BUTTON_DOWN : BR_EVENT_MOUSE_BUTTON_UP, BR_MOUSE_LEFT, x, y);
    br_headless_inject(&b);
}

static void inject_right_click(float x, float y) {
    br_event m = br_event_mouse_move(x, y, 1, 1);
    br_event d = br_event_mouse_button(BR_EVENT_MOUSE_BUTTON_DOWN, BR_MOUSE_RIGHT, x, y);
    br_event u = br_event_mouse_button(BR_EVENT_MOUSE_BUTTON_UP, BR_MOUSE_RIGHT, x, y);
    br_headless_inject(&m);
    br_headless_inject(&d);
    br_headless_inject(&u);
}

/* Clicks the open menu's item called `label` (items are a row + 2 high, separators 7). */
static bool menu_click(br_engine *e, const char *label) {
    const br_ui *ui = &ED.ui;
    float y = ui->menu.rect.y + 1;
    int k;
    for (k = 0; k < ui->menu.count; ++k) {
        const br_ui_menu_entry *it = &ui->menu.items[k];
        float h = it->label[0] ? ui->style.row + 2 : 7.0f;
        if (it->parent != -1) continue;
        if (strcmp(it->label, label) == 0) {
            inject_click(ui->menu.rect.x + 30, y + h / 2);
            frames(e, 3);
            return true;
        }
        y += h;
    }
    return false;
}

/* Right-click menus, copy and paste, the console. */
static int test_menus_console(br_engine *e) {
    editor *ed = &ED;
    int failures = 0, i;
    uint32_t before = ed->doc.count;
    br_entity ball = ed->doc.order[3];
    /* a hierarchy row: its menu, then Duplicate */
    ed->doc.selected = BR_ENTITY_NULL;
    inject_right_click(60, HIER_ROW_Y(3));
    frames(e, 2);
    if (!br_ui_menu_is_open(&ed->ui, "entity") || ed->doc.selected != ball) { printf("FAIL: right-clicking a row didn't open its menu\n"); ++failures; }
    br_image_save_bmp(&e->framebuffer, "editor_menu.bmp");
    if (!menu_click(e, "Duplicate") || ed->doc.count != before + 1 || br_ui_menu_active(&ed->ui))
        { printf("FAIL: Duplicate from the menu (%u entities)\n", ed->doc.count); ++failures; }
    ed_cmd_undo(ed);
    frames(e, 2);
    /* Add Component, from its submenu */
    ball = ed->doc.order[3];                                  /* (undo rebuilt the entities) */
    inject_right_click(60, HIER_ROW_Y(3));
    frames(e, 2);
    {
        int k, sub = -1, item = -1;
        for (k = 0; k < ed->ui.menu.count; ++k) {
            if (strcmp(ed->ui.menu.items[k].label, "Add Component") == 0) sub = k;
            if (sub >= 0 && ed->ui.menu.items[k].parent == sub && strcmp(ed->ui.menu.items[k].label, "Label") == 0) item = k;
        }
        if (sub < 0 || item < 0) { printf("FAIL: no Add Component > Label in the menu\n"); ++failures; }
        else {
            float y = ed->ui.menu.rect.y + 1;
            for (k = 0; k < sub; ++k) if (ed->ui.menu.items[k].parent == -1) y += ed->ui.menu.items[k].label[0] ? 18.0f : 7.0f;
            inject_move(ed->ui.menu.rect.x + 30, y + 9);         /* hover: the submenu opens */
            frames(e, 2);
            y = ed->ui.menu.sub_rect.y + 1;
            for (k = sub + 1; k < item; ++k) if (ed->ui.menu.items[k].parent == sub) y += 18.0f;
            inject_move(ed->ui.menu.sub_rect.x + 30, y + 9);
            frames(e, 1);
            br_image_save_bmp(&e->framebuffer, "editor_submenu.bmp");
            inject_click(ed->ui.menu.sub_rect.x + 30, y + 9);
            frames(e, 3);
            if (!br_has(&ed->doc.world, ball, ed->doc.ids.label)) { printf("FAIL: Add Component from the submenu\n"); ++failures; }
        }
    }
    ed_cmd_undo(ed);
    frames(e, 2);
    ball = ed->doc.order[3];
    /* copy, then paste into empty space in the scene view */
    ed->doc.selected = ball;
    ed_cmd_copy(ed);
    {   /* copied entities are scene text on the system clipboard */
        char *text = br_clipboard_get(e);
        if (!text || strncmp(text, "# Bronto scene", 14) != 0 || !strstr(text, "Ball")) { printf("FAIL: copying an entity didn't put it on the clipboard\n"); ++failures; }
        br_free(text);
    }
    {
        br_vec2 at = br_v2(ed->view.rect.x + 14, ed->view.rect.y + 40), world = ed_view_to_world(&ed->view, at);
        if (ed_doc_pick(&ed->doc, world) != BR_ENTITY_NULL) { printf("FAIL: (test) the view's corner isn't empty\n"); ++failures; }
        before = ed->doc.count;
        inject_right_click(at.x, at.y);
        frames(e, 2);
        if (!br_ui_menu_is_open(&ed->ui, "view")) { printf("FAIL: right-clicking empty space didn't open the view menu\n"); ++failures; }
        if (!menu_click(e, "Paste Here") || ed->doc.count != before + 1 ||
            br_v2_dist(br_world_transform(&ed->doc.world, ed->doc.selected).position, world) > 0.5f)
            { printf("FAIL: Paste Here\n"); ++failures; }
        ed_cmd_paste(ed, NULL, ball);                         /* as a child */
        if (br_parent_of(&ed->doc.world, ed->doc.selected) != ball) { printf("FAIL: paste as a child\n"); ++failures; }
        frames(e, 2);
        ed_cmd_undo(ed);
        ed_cmd_undo(ed);
        frames(e, 2);
    }
    /* components: copy values from one, paste them on another, reset */
    {
        br_entity a = ed->doc.order[3], b = ed->doc.order[4];
        br_shape *sa = (br_shape *)br_get(&ed->doc.world, a, ed->doc.ids.shape), *sb = (br_shape *)br_get(&ed->doc.world, b, ed->doc.ids.shape);
        if (!sa || !sb || !ed_component_copy(ed, a, ed->doc.ids.shape) || !ed_component_paste(ed, b, ed->doc.ids.shape) ||
            sb->kind != sa->kind || sb->color.r != sa->color.r || sb->size.x != sa->size.x) { printf("FAIL: component copy / paste\n"); ++failures; }
        if (sb) {
            const br_shape *def = (const br_shape *)br_component_get_desc(&ed->doc.world, ed->doc.ids.shape)->defaults;
            ed_component_reset(ed, b, ed->doc.ids.shape, -1);
            if (def && (sb->size.x != def->size.x || sb->kind != def->kind)) { printf("FAIL: component reset\n"); ++failures; }
        }
        frames(e, 2);
        ed_cmd_undo(ed);
        frames(e, 2);
    }
    {   /* scene text copied elsewhere (another editor) pastes as entities */
        char *text = ed_doc_copy(&ed->doc, ed->doc.order[2]), *at, renamed[8192];
        char name[64];
        if (text && (at = strstr(text, "Title")) != NULL && strlen(text) < sizeof(renamed) - 8) {
            snprintf(renamed, sizeof(renamed), "%.*sFromAfar%s", (int)(at - text), text, at + 5);
            br_clipboard_set(e, renamed);
            before = ed->doc.count;
            ed_cmd_paste(ed, NULL, BR_ENTITY_NULL);
            if (ed->doc.count != before + 1 || strcmp(ed_doc_name(&ed->doc, ed->doc.selected, name, sizeof(name)), "FromAfar") != 0)
                { printf("FAIL: pasting scene text from the clipboard\n"); ++failures; }
            frames(e, 2);
            ed_cmd_undo(ed);
            frames(e, 2);
        } else { printf("FAIL: (test) copying Title\n"); ++failures; }
        br_free(text);
    }
    {   /* text fields paste from the clipboard: rename by pasting a name */
        br_event k;
        char name[64];
        ed->doc.selected = ed->doc.order[3];
        ed_cmd_rename(ed);
        frames(e, 2);
        br_clipboard_set(e, "Pasted Name\nignored");
        k = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_V, BR_MOD_CONTROL, false); br_headless_inject(&k);
        k = br_event_key(BR_EVENT_KEY_UP, BR_KEY_V, BR_MOD_CONTROL, false); br_headless_inject(&k);
        frames(e, 1);
        k = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_ENTER, 0, false); br_headless_inject(&k);
        k = br_event_key(BR_EVENT_KEY_UP, BR_KEY_ENTER, 0, false); br_headless_inject(&k);
        frames(e, 2);
        if (strcmp(ed_doc_name(&ed->doc, ed->doc.order[3], name, sizeof(name)), "Pasted Name") != 0)
            { printf("FAIL: pasting into the Name field (%s)\n", name); ++failures; }
        ed_cmd_undo(ed);
        frames(e, 2);
    }
    if (ed->doc.count != 7) { printf("FAIL: the menu tests left %u entities\n", ed->doc.count); ++failures; }
    /* the console: what goes to stdout and stderr shows up, sorted by level */
    ed_console_clear(&ed->console);
    printf("hello from stdout\n");
    fprintf(stderr, "[WARN ] fake.c:12: careful now\n");
    {   /* (the reader thread is quick, but not instant: look for a while) */
        const ed_con_line *out = NULL, *warn = NULL;
        uint32_t k;
        for (i = 0; i < 400 && (!out || !warn) && ed->console_capturing; ++i) {
            frames(e, 1);
            for (k = 0; k < ed->console.count; ++k) {
                const ed_con_line *l = ed_console_line(&ed->console, k);
                if (strcmp(l->text, "hello from stdout") == 0 && l->kind == ED_CON_OUT) out = l;
                if (strcmp(l->text, "careful now") == 0 && l->level == ED_LEVEL_WARN) warn = l;
            }
        }
        if (!ed->console_capturing) printf("note: output capture isn't available here\n");
        else if (!out || !warn) { printf("FAIL: the console didn't get stdout and stderr (%u lines)\n", ed->console.count); ++failures; }
    }
    ed->bottom_tab = ED_TAB_CONSOLE;
    frames(e, 2);
    {
        layout l = make_layout(&ED);
        inject_right_click(l.console.x + 100, l.console.y + 24 + 6 + 6);   /* the first line */
        frames(e, 2);
        if (!br_ui_menu_is_open(&ed->ui, "console") || (ed->console.count && ed->console_selected < 0))
            { printf("FAIL: the console's menu\n"); ++failures; }
        br_image_save_bmp(&e->framebuffer, "editor_console.bmp");
        if (ed->console.count && ed->console_selected >= 0) {         /* Copy Line */
            const ed_con_line *first = ed_console_line(&ed->console, (uint32_t)ed->console_selected);
            char *text;
            menu_click(e, "Copy Line");
            text = br_clipboard_get(e);
            if (!text || strcmp(text, first->text) != 0) { printf("FAIL: copying a console line (%s)\n", text ? text : "nothing"); ++failures; }
            br_free(text);
        }
        br_ui_menu_close(&ed->ui);
    }
    ed->bottom_tab = ED_TAB_FILES;
    frames(e, 2);
    return failures;
}

/* The middle of tile `i` in the asset browser (toolbar row, breadcrumb row, then tiles). */
static br_vec2 tile_center(const br_engine *e, int i) {
    layout l = make_layout(&ED);
    BR_UNUSED(e);
    return br_v2(l.assets.x + 6 + 76.0f * (float)i + 37, l.assets.y + 6 + 19 + 19 + 2 + 35);
}

/* A small project to browse: an image, a sound, folders. */
static bool make_test_project(void) {
    br_image img;
    br_renderer r;
    br_sound beep;
    bool ok;
    br_fs_mkdir("editor_project");
    br_fs_mkdir("editor_project/art");
    br_fs_mkdir("editor_project/sfx");
    br_image_init(&img, 24, 24);
    br_renderer_init(&r, &img);
    br_clear(&r, br_rgba(0, 0, 0, 0));
    br_fill_circle(&r, br_v2(12, 13), 10, br_rgb(90, 200, 110));
    br_fill_circle(&r, br_v2(8, 10), 3, BR_WHITE);
    br_fill_circle(&r, br_v2(16, 10), 3, BR_WHITE);
    br_fill_circle(&r, br_v2(8, 10), 1.5f, BR_BLACK);
    br_fill_circle(&r, br_v2(16, 10), 1.5f, BR_BLACK);
    ok = br_image_save_tga(&img, "editor_project/art/hero.tga");
    br_image_destroy(&img);
    ok = ok && br_sound_generate(&beep, BR_WAVE_SQUARE, 660, 0.25f, 0.3f, 22050);
    ok = ok && br_sound_save_wav(&beep, "editor_project/sfx/beep.wav");
    br_sound_destroy(&beep);
    return ok;
}

static int test_assets(br_engine *e) {
    editor *ed = &ED;
    int failures = 0, i;
    uint32_t before;
    br_vec2 p, q, drop;
    const br_sprite *sp;
    char prefab[ED_PATH_MAX], full[ED_PATH_MAX];
    if (!make_test_project()) { printf("FAIL: couldn't write the test project\n"); return 1; }
    ed_cmd_new(ed);
    ed_assets_destroy(ed);
    ed_assets_init(ed, "editor_project");
    frames(e, 2);
    if (ed->assets.list.count < 2 || strcmp(ed->assets.list.entries[0].name, "art") != 0 || !ed->assets.list.entries[1].is_dir)
        { printf("FAIL: the project doesn't list its folders first\n"); ++failures; }
    p = tile_center(e, 0);                                    /* double-click "art" */
    inject_click(p.x, p.y); frames(e, 1);
    inject_click(p.x, p.y); frames(e, 2);
    if (strcmp(ed->assets.dir, "art") != 0) { printf("FAIL: double-clicking a folder didn't open it\n"); ++failures; }

    /* drag hero.tga into the middle of the scene view */
    before = ed->doc.count;
    p = tile_center(e, 0);
    drop = br_v2(ed->view.rect.x + ed->view.rect.w / 2 + 30, ed->view.rect.y + ed->view.rect.h / 2);
    inject_move(p.x, p.y); inject_button(true, p.x, p.y); frames(e, 1);
    for (i = 1; i <= 8; ++i) {
        q = br_v2_lerp(p, drop, (float)i / 8.0f);
        inject_move(q.x, q.y);
        frames(e, 1);
    }
    if (!br_ui_dragging(&ed->ui, ED_ASSET_DRAG)) { printf("FAIL: dragging a tile didn't start a drag\n"); ++failures; }
    br_image_save_bmp(&e->framebuffer, "editor_drag.bmp");
    inject_button(false, drop.x, drop.y); frames(e, 2);
    sp = ed->doc.count == before + 1 ? (const br_sprite *)br_get(&ed->doc.world, ed->doc.order[before], ed->doc.ids.sprite) : NULL;
    if (!sp || strcmp(sp->image, "art/hero.tga") != 0) { printf("FAIL: dropping an image didn't make a sprite\n"); ++failures; }
    else {
        const br_transform *t = (const br_transform *)br_get(&ed->doc.world, ed->doc.order[before], ed->doc.ids.transform);
        br_vec2 want = ed_view_to_world(&ed->view, drop);
        if (!t || br_v2_dist(t->position, want) > 1) { printf("FAIL: the sprite isn't where it was dropped\n"); ++failures; }
    }

    /* prefabs: save the new sprite, place two copies */
    if (!ed_assets_save_prefab(ed) || !br_path_has_ext(ed->assets.selected, ".scene")) { printf("FAIL: save prefab\n"); ++failures; }
    memcpy(prefab, ed->assets.selected, sizeof(prefab));       /* art/hero.scene (or hero (2).scene on a rerun) */
    if (!br_fs_exists("editor_project/" BR_PROJECT_FILE)) { printf("FAIL: no project file for a scene in a subfolder\n"); ++failures; }
    before = ed->doc.count;
    ed_assets_drop(ed, prefab, br_v2(40, 140));
    ed_assets_drop(ed, prefab, br_v2(280, 140));
    if (ed->doc.count != before + 2) { printf("FAIL: placing a prefab\n"); ++failures; }

    /* file operations */
    if (!ed_assets_new_folder(ed) || strcmp(ed->assets.selected, "art/New folder") != 0) { printf("FAIL: new folder\n"); ++failures; }
    if (!ed_assets_rename(ed, "enemies") || !br_fs_is_dir("editor_project/art/enemies")) { printf("FAIL: rename\n"); ++failures; }
    if (ed_assets_rename(ed, "hero.tga")) { printf("FAIL: rename overwrote a file\n"); ++failures; }
    if (!ed_assets_delete(ed) || br_fs_exists("editor_project/art/enemies") || !br_fs_is_dir("editor_project/.trash/enemies"))
        { printf("FAIL: delete to the trash\n"); ++failures; }

    /* sounds: a Sound entity plays when the scene starts and stops with it */
    before = ed->doc.count;
    if (!ed_assets_drop(ed, "sfx/beep.wav", br_v2(160, 30)) || ed->doc.count != before + 1) { printf("FAIL: dropping a sound\n"); ++failures; }
    frames(e, 2);
    ed_cmd_play(ed);
    frames(e, 3);
    if (br_mixer_active_voices(&e->mixer) < 1) { printf("FAIL: the Sound didn't play\n"); ++failures; }
    ed_cmd_play(ed);
    frames(e, 10);
    if (br_mixer_active_voices(&e->mixer) != 0) { printf("FAIL: Stop didn't silence the scene\n"); ++failures; }
    ed_assets_play(ed, "sfx/beep.wav");                         /* preview */
    if (br_mixer_active_voices(&e->mixer) != 1) { printf("FAIL: previewing a sound\n"); ++failures; }

    {   /* scripts: drop one on the sprite, play, stop; and a broken one reports its error */
        br_entity hero = ed->doc.order[0];
        const br_transform *t = (const br_transform *)br_get(&ed->doc.world, hero, ed->doc.ids.transform);
        float x0 = t ? t->position.x : 0;
        const char mover[] = "var steps = 0\nfunc update(dt) {\n    steps += 1\n    self.x += 2\n}\n";
        const char broken[] = "func update(dt) {\n    self.x += speeed\n}\n";
        br_fs_mkdir("editor_project/scripts");
        br_file_write_all("editor_project/scripts/mover.bs", mover, sizeof(mover) - 1);
        br_file_write_all("editor_project/scripts/broken.bs", broken, sizeof(broken) - 1);
        if (!t || !ed_assets_drop(ed, "scripts/mover.bs", t->position) || !br_has(&ed->doc.world, hero, br_component_find(&ed->doc.world, "Script")))
            { printf("FAIL: dropping a script on an entity didn't attach it\n"); ++failures; }
        frames(e, 2);
        ed_cmd_play(ed);
        frames(e, 10);
        t = (const br_transform *)br_get(&ed->doc.world, ed->doc.order[0], ed->doc.ids.transform);
        if (!t || t->position.x < x0 + 15) { printf("FAIL: the script didn't run while playing (x %.1f)\n", t ? (double)t->position.x : -1.0); ++failures; }
        ed_cmd_play(ed);
        frames(e, 2);
        t = (const br_transform *)br_get(&ed->doc.world, ed->doc.order[0], ed->doc.ids.transform);
        if (!t || t->position.x != x0) { printf("FAIL: Stop didn't undo what the script did\n"); ++failures; }
        ed_assets_drop(ed, "scripts/broken.bs", br_v2(300, 20));
        ed_cmd_play(ed);
        frames(e, 3);
        if (!strstr(ed->status, "broken.bs:2: unknown name 'speeed'")) { printf("FAIL: a script error wasn't shown (%s)\n", ed->status); ++failures; }
        {   /* ... and it's in the console, as an error that leads to the script */
            uint32_t k;
            char file[ED_PATH_MAX];
            int line = 0;
            bool found = false;
            for (i = 0; i < 300 && !found; ++i) {
                frames(e, 1);
                for (k = 0; k < ed->console.count && !found; ++k) {
                    const ed_con_line *l = ed_console_line(&ed->console, k);
                    found = l->level == ED_LEVEL_ERROR && strstr(l->text, "unknown name 'speeed'") &&
                            ed_console_location(l->text, file, sizeof(file), &line) && line == 2;
                }
            }
            if (ed->console_capturing && !found) { printf("FAIL: the script error isn't in the console\n"); ++failures; }
            snprintf(ed->prefs.script_editor, sizeof(ed->prefs.script_editor), "system");
            if (!ed_open_script_named(ed, "broken.bs", 2) || !strstr(br_headless_last_opened(), "broken.bs"))
                { printf("FAIL: opening a script named in a message (%s)\n", br_headless_last_opened()); ++failures; }
            ed->bottom_tab = ED_TAB_CONSOLE;
            frames(e, 2);
            br_image_save_bmp(&e->framebuffer, "editor_console_error.bmp");
            ed->bottom_tab = ED_TAB_FILES;
        }
        br_image_save_bmp(&e->framebuffer, "editor_script_error.bmp");
        {   /* the chosen script editor is run with the file and the line of the mistake */
            ed_assets_select(ed, "scripts/broken.bs");
            frames(e, 1);
            snprintf(ed->prefs.script_editor, sizeof(ed->prefs.script_editor), "myeditor --goto {line} \"{file}\"");
            ed_assets_edit(ed, "scripts/broken.bs");
            if (!strstr(br_headless_last_launched(), "myeditor --goto 2 \"") || !strstr(br_headless_last_launched(), "broken.bs\""))
                { printf("FAIL: the script editor command (%s)\n", br_headless_last_launched()); ++failures; }
            snprintf(ed->prefs.script_editor, sizeof(ed->prefs.script_editor), "system");
            ed_assets_edit(ed, "scripts/mover.bs");            /* "system": the system's default program */
            if (!strstr(br_headless_last_opened(), "mover.bs")) { printf("FAIL: opening with the default program\n"); ++failures; }
            ed->prefs.script_editor[0] = 0;                    /* empty: the Scripts tab */
            ed_assets_edit(ed, "scripts/broken.bs");
            if (!ed_code_current(ed) || strcmp(ed_code_current(ed)->name, "broken.bs") != 0 || ed->bottom_tab != ED_TAB_DEBUG ||
                ed_code_line_of(ed_code_current(ed), ed_code_current(ed)->cursor) != 1 || ed_code_current(ed)->error_line != 2)
                { printf("FAIL: opening a script in the Scripts tab, at its mistake\n"); ++failures; }
            ed_code_close_all(ed);
            ed->bottom_tab = ED_TAB_FILES;
            ed_settings_open(ed);
            frames(e, 2);
            br_image_save_bmp(&e->framebuffer, "editor_settings.bmp");
            ed->settings_visible = false;
        }
        ed_cmd_play(ed);
        frames(e, 2);
    }
    {   /* a file's menu */
        p = tile_center(e, 0);
        inject_right_click(p.x, p.y);
        frames(e, 2);
        if (!br_ui_menu_is_open(&ed->ui, "asset") || !ed->assets.selected[0]) { printf("FAIL: right-clicking a file\n"); ++failures; }
        br_ui_menu_close(&ed->ui);
        frames(e, 1);
    }
    ed_assets_select(ed, "art/hero.tga");
    ed->doc.selected = ed->doc.order[0];
    ed_cmd_frame(ed);
    frames(e, 2);
    br_image_save_bmp(&e->framebuffer, "editor_assets.bmp");

    /* Load Project: the OS dialog (scripted here), then the built-in picker where there is none */
    br_headless_set_folder_answer("editor_project/art", true);
    ed_cmd_load_project(ed);
    if (ed->pending == ED_PENDING_PROJECT) ed_cmd_answer(ed, 1);
    if (strcmp(br_path_name(ed->assets.root), "art") != 0 || ed->assets.dir[0] || !br_fs_exists("editor_project/art/" BR_PROJECT_FILE))
        { printf("FAIL: Load Project didn't open the chosen folder (%s)\n", ed->assets.root); ++failures; }
    br_headless_set_folder_answer(NULL, true);
    ed_cmd_load_project(ed);                                    /* cancelled: nothing changes */
    if (strcmp(br_path_name(ed->assets.root), "art") != 0 || ed->picker.open) { printf("FAIL: cancelling Load Project\n"); ++failures; }
    br_headless_set_folder_answer(NULL, false);
    ed_cmd_load_project(ed);
    if (!ed->picker.open) { printf("FAIL: no built-in picker without an OS dialog\n"); ++failures; }
    frames(e, 2);
    br_image_save_bmp(&e->framebuffer, "editor_picker.bmp");
    {   /* go up to editor_project, then choose it */
        layout l = make_layout(&ED);
        float bx = floorf((l.screen.w - 480) / 2), by = floorf((l.screen.h - 380) / 2);
        inject_click(bx + 6 + 50, by + 6 + 19 * 2 + 8);           /* "Up" */
        frames(e, 2);
        inject_click(bx + 240, by + 6 + 19 * 2 + 8);              /* "Open this folder" */
        frames(e, 2);
    }
    if (ed->picker.open || strcmp(br_path_name(ed->assets.root), "editor_project") != 0)
        { printf("FAIL: choosing a folder in the built-in picker (%s)\n", ed->assets.root); ++failures; }

    /* double-click a scene: opens it (after the unsaved-changes question) */
    ed_assets_activate(ed, prefab);
    if (ed->pending == ED_PENDING_OPEN) ed_cmd_answer(ed, 1);
    ed_asset_path(ed, prefab, full, sizeof(full));
    if (ed->doc.count != 1 || strcmp(ed->doc.path, full) != 0) { printf("FAIL: opening a scene from the browser\n"); ++failures; }
    frames(e, 2);
    return failures;
}

/* The Build menu: script checks, a game built from the test project, run headless; load_scene and quit in Play. */
static int test_build(br_engine *e) {
    editor *ed = &ED;
    int failures = 0, scripts = 0, errors, i;
    char out[1024], cmd[1200], player[1024];
    br_project p;
    uint32_t k;
    bool found = false;
    const char scene[] =
        "bronto_scene 1\nentity\n  Name\n    text = \"Mover\"\n  Transform\n    position = 40 40\n"
        "  Shape\n    size = 10 10\n  Script\n    file = \"scripts/mover.bs\"\n";
    const char level2[] =
        "bronto_scene 1\nentity\n  Name\n    text = \"A\"\nentity\n  Name\n    text = \"B\"\nentity\n  Name\n    text = \"C\"\n";
    br_file_write_all("editor_project/main.scene", scene, sizeof(scene) - 1);
    br_file_write_all("editor_project/level2.scene", level2, sizeof(level2) - 1);
    p = ed->project;
    snprintf(p.name, sizeof(p.name), "Test Game");
    snprintf(p.main_scene, sizeof(p.main_scene), "main.scene");
    if (!ed_project_save(ed, &p) || strcmp(ed->project.name, "Test Game") != 0) { printf("FAIL: saving project settings\n"); ++failures; }
    /* the toolbar's Build menu */
    inject_click(906, TOOLBAR_H / 2);
    frames(e, 2);
    if (!br_ui_menu_is_open(&ed->ui, "build")) { printf("FAIL: the Build button didn't open its menu\n"); ++failures; }
    br_image_save_bmp(&e->framebuffer, "editor_build_menu.bmp");
    br_ui_menu_close(&ed->ui);
    frames(e, 1);
    ed_project_settings_open(ed);
    frames(e, 2);
    br_image_save_bmp(&e->framebuffer, "editor_project_settings.bmp");
    ed->project_visible = false;
    /* broken.bs stops the build, and the console says where */
    ed_console_clear(&ed->console);
    if (ed_build(ed, ed_host_target(), out, sizeof(out))) { printf("FAIL: a project with a broken script built\n"); ++failures; }
    for (i = 0; i < 300 && !found; ++i) {
        frames(e, 1);
        for (k = 0; k < ed->console.count && !found; ++k) found = strstr(ed_console_line(&ed->console, k)->text, "scripts/broken.bs:2") != NULL;
    }
    if (ed->console_capturing && !found) { printf("FAIL: the build's script error isn't in the console\n"); ++failures; }
    {
        const char fixed[] = "var speed: number = 2\nfunc update(dt: number) -> void {\n    self.x += speed\n}\n";
        br_file_write_all("editor_project/scripts/broken.bs", fixed, sizeof(fixed) - 1);
    }
    errors = ed_check_scripts(ed, &scripts);
    if (errors || scripts < 2) { printf("FAIL: Check Scripts (%d errors in %d)\n", errors, scripts); ++failures; }   /* (a rerun has more) */
    p.strict_typing = true;                                      /* strict: mover.bs has no types */
    ed_project_save(ed, &p);
    if (ed_check_scripts(ed, NULL) < 1) { printf("FAIL: strict typing in Check Scripts\n"); ++failures; }
    p.strict_typing = false;
    ed_project_save(ed, &p);
    if (!ed_find_player(ed, ed_host_target(), player, sizeof(player))) {
        printf("note: no bronto_player beside the editor, so no game is built (make tools)\n");
    } else if (!ed_build(ed, ed_host_target(), out, sizeof(out))) {
        printf("FAIL: building the game (%s)\n", ed->status); ++failures;
    } else {
        char run[1100];
        if (ed_host_target() == ED_TARGET_MACOS) snprintf(run, sizeof(run), "%s/Contents/MacOS/Test Game", out);
        else snprintf(run, sizeof(run), "%s", out);
        if (!br_fs_exists(run)) { printf("FAIL: the built game isn't at %s\n", run); ++failures; }
        snprintf(cmd, sizeof(cmd), "\"%s\" --headless --frames 30 --screenshot build_game.bmp", run);
        fflush(stdout);
        if (system(cmd) != 0) { printf("FAIL: the built game didn't run (%s)\n", cmd); ++failures; }
        if (!br_fs_exists("build_game.bmp")) { printf("FAIL: the built game didn't save its screenshot\n"); ++failures; }
    }
    if (!ed_find_player(ed, ED_TARGET_WEB, player, sizeof(player))) {
        printf("note: no web player beside the editor, so no web page is built (make web)\n");
    } else if (!ed_build(ed, ED_TARGET_WEB, out, sizeof(out))) {
        printf("FAIL: building for the web (%s)\n", ed->status); ++failures;
    } else {
        size_t size = 0;
        char *page = (char *)br_file_read_all(out, &size);
        if (!page || size < 100000 || !strstr(page, "<title>Test Game</title>") || !strstr(page, "data:image/png;base64,") ||
            strstr(page, "{{")) { printf("FAIL: the web page %s isn't right (%u bytes)\n", out, (unsigned)size); ++failures; }
        br_free(page);
    }
    /* load_scene() and quit() while playing */
    {
        const char sw[] = "func update(dt) {\n    load_scene(\"level2.scene\")\n}\n";
        const char q[] = "var n = 0\nfunc update(dt) {\n    n += 1\n    if n == 3 { quit() }\n}\n";
        br_entity s;
        br_file_write_all("editor_project/scripts/switch.bs", sw, sizeof(sw) - 1);
        br_file_write_all("editor_project/scripts/quit.bs", q, sizeof(q) - 1);
        ed_cmd_new(ed);
        if (ed->pending != ED_PENDING_NONE) ed_cmd_answer(ed, 1);
        s = ed_doc_create(&ed->doc, "Switcher", br_v2(0, 0));
        snprintf(((br_script *)br_add(&ed->doc.world, s, br_component_find(&ed->doc.world, "Script"), NULL))->file, 64, "scripts/switch.bs");
        frames(e, 2);
        ed_cmd_play(ed);
        frames(e, 4);
        if (!ed->doc.playing || ed->doc.count != 3) { printf("FAIL: load_scene() while playing (%u entities)\n", ed->doc.count); ++failures; }
        ed_cmd_play(ed);
        frames(e, 2);
        if (ed->doc.count != 1) { printf("FAIL: Stop after load_scene() (%u entities)\n", ed->doc.count); ++failures; }
        snprintf(((br_script *)br_get(&ed->doc.world, ed->doc.order[0], br_component_find(&ed->doc.world, "Script")))->file, 64, "scripts/quit.bs");
        frames(e, 2);
        ed_cmd_play(ed);
        frames(e, 10);
        if (ed->doc.playing) { printf("FAIL: quit() didn't stop playing\n"); ++failures; ed_cmd_play(ed); }
        ed_cmd_new(ed);
        if (ed->pending != ED_PENDING_NONE) ed_cmd_answer(ed, 1);
    }
    return failures;
}

/* Resizing and collapsing the panels with the mouse. */
static br_vec2 mid_of(br_rect r) { return br_v2(r.x + r.w / 2, r.y + r.h / 2); }
static int test_panels(br_engine *e) {
    editor *ed = &ED;
    int failures = 0;
    layout l = make_layout(ed), before = l;
    br_vec2 p;
    if (l.hierarchy.w != 200 || l.inspector.w != 300 || l.assets.h != 196 || l.preview.w != 230)
        { printf("FAIL: the panels' first sizes\n"); ++failures; }
    /* drag the scene list's edge 60 px right: it widens, the view narrows */
    inject_drag(e, br_v2(l.hierarchy.w + 1, 300), br_v2(l.hierarchy.w + 61, 300));
    l = make_layout(ed);
    if (fabsf(l.hierarchy.w - 260) > 1 || fabsf(l.view.w - (before.view.w - 60)) > 1)
        { printf("FAIL: dragging the scene list's edge (%.0f wide)\n", (double)l.hierarchy.w); ++failures; }
    /* most of the way closed: it collapses to a strip */
    inject_drag(e, br_v2(l.hierarchy.w + 1, 300), br_v2(30, 300));
    l = make_layout(ed);
    if (ed->panels.open[ED_PANEL_SCENE] || l.hierarchy.w != STRIP_W) { printf("FAIL: dragging the scene list shut didn't collapse it\n"); ++failures; }
    /* the inspector's arrow, and a double-click on the bottom edge, collapse those */
    p = mid_of(l.button[ED_PANEL_INSPECTOR]);
    inject_click(p.x, p.y);
    frames(e, 2);
    p = mid_of(l.edge[ED_PANEL_BOTTOM]);
    inject_click(p.x, p.y);
    frames(e, 2);
    inject_click(p.x, p.y);
    frames(e, 2);
    l = make_layout(ed);
    if (ed->panels.open[ED_PANEL_INSPECTOR] || l.inspector.w != STRIP_W) { printf("FAIL: the inspector's collapse arrow\n"); ++failures; }
    if (ed->panels.open[ED_PANEL_BOTTOM] || l.tabs.y + TABS_H != l.status.y) { printf("FAIL: double-clicking the bottom edge didn't collapse it\n"); ++failures; }
    if (l.view.w < before.view.w + 250 || l.view.h < before.view.h + 190) { printf("FAIL: the view didn't take the room\n"); ++failures; }
    br_image_save_bmp(&e->framebuffer, "editor_panels.bmp");
    /* open them again: the strip, and a tab */
    inject_click(10, 200);
    frames(e, 2);
    l = make_layout(ed);
    if (!ed->panels.open[ED_PANEL_SCENE] || fabsf(l.hierarchy.w - 260) > 1) { printf("FAIL: clicking the strip didn't bring the scene list back as it was\n"); ++failures; }
    inject_click(l.tabs.x + 4 + 60 + 4 + 30, l.tabs.y + TABS_H / 2);        /* Console */
    frames(e, 2);
    if (!ed->panels.open[ED_PANEL_BOTTOM] || ed->bottom_tab != ED_TAB_CONSOLE) { printf("FAIL: clicking a tab didn't open the bottom panel\n"); ++failures; }
    /* the bottom edge, dragged up 50 px */
    l = make_layout(ed);
    inject_drag(e, br_v2(l.tabs.x + 300, l.tabs.y - 1), br_v2(l.tabs.x + 300, l.tabs.y - 51));
    l = make_layout(ed);
    if (fabsf(l.console.h - 246) > 1) { printf("FAIL: dragging the bottom edge (%.0f high)\n", (double)l.console.h); ++failures; }
    /* the preview, beside the files */
    ed->bottom_tab = ED_TAB_FILES;
    frames(e, 2);
    l = make_layout(ed);
    p = mid_of(l.button[ED_PANEL_PREVIEW]);
    inject_click(p.x, p.y);
    frames(e, 2);
    l = make_layout(ed);
    if (ed->panels.open[ED_PANEL_PREVIEW] || l.preview.w != STRIP_W) { printf("FAIL: the preview's collapse arrow\n"); ++failures; }
    br_image_save_bmp(&e->framebuffer, "editor_panels2.bmp");
    /* too big for the window: the view still keeps its share */
    ed->panels.size[ED_PANEL_SCENE] = 900;
    ed->panels.open[ED_PANEL_INSPECTOR] = true;
    l = make_layout(ed);
    if (l.view.w < VIEW_MIN_W - 2 || l.inspector.w < k_panel_min[ED_PANEL_INSPECTOR]) { printf("FAIL: oversized panels squeezed the view (%.0f)\n", (double)l.view.w); ++failures; }
    ed_panels_default(&ed->panels);
    frames(e, 2);
    return failures;
}

/* The 3D view: a scene of meshes, a camera and a light; clicking, the gizmo,
 * undo, play, a model dropped in, and reopening the scene in 3D. */
static void inject_key(br_key k) {
    br_event d = br_event_key(BR_EVENT_KEY_DOWN, k, 0, false), u = br_event_key(BR_EVENT_KEY_UP, k, 0, false);
    br_headless_inject(&d);
    br_headless_inject(&u);
}

static bool differs(const br_engine *e, br_vec2 p, br_color bg) {
    br_color c = br_image_get(&e->framebuffer, (int)p.x, (int)p.y);
    return abs((int)c.r - bg.r) + abs((int)c.g - bg.g) + abs((int)c.b - bg.b) > 30;
}

static int test_view3d(br_engine *e) {
    editor *ed = &ED;
    ed_doc *d = &ed->doc;
    int failures = 0;
    layout l = make_layout(ed);
    br_vec2 mid = br_v2(l.view.x + l.view.w / 2, l.view.y + l.view.h / 2), s, tip;
    br_entity cube, ground, cam, falling;
    br_transform3d *t;
    br_component rb3 = br_component_find(&d->world, "RigidBody3D"), col3 = br_component_find(&d->world, "Collider3D");

    ed_cmd_new(ed);
    ed_view_set_3d(ed, true);
    ed_view3d_reset(&ed->view);
    ground = ed_new3d_at(ed, ED_NEW_PLANE, br_v3(0, 0, 0));
    cube = ed_new3d_at(ed, ED_NEW_CUBE, br_v3(0, 0, 0));
    cam = ed_new3d_at(ed, ED_NEW_CAMERA, br_v3(0, 0, 0));
    ed_new3d_at(ed, ED_NEW_LIGHT, br_v3(0, 0, 0));
    ((br_mesh3d *)br_get(&d->world, cube, d->ids3.mesh))->color = br_rgb(240, 120, 40);
    ((br_mesh3d *)br_get(&d->world, ground, d->ids3.mesh))->color = br_rgb(80, 150, 80);
    d->selected = BR_ENTITY_NULL;
    frames(e, 3);
    br_image_save_bmp(&e->framebuffer, "editor_3d.bmp");
    if (!ed_view3d_project(ed, br_v3(0, 0.5f, 0), &s) || br_v2_dist(s, mid) > 30) { printf("FAIL: 3D: the cube isn't in the middle of the view\n"); ++failures; }
    if (!differs(e, mid, br_rgb(36, 40, 48))) { printf("FAIL: 3D: nothing drawn in the middle of the view\n"); ++failures; }
    /* click it: selected */
    ed_view3d_project(ed, br_v3(0, 0.5f, 0), &s);
    inject_click(s.x, s.y);
    frames(e, 2);
    if (d->selected != cube) { printf("FAIL: 3D: clicking the cube didn't select it\n"); ++failures; }
    /* the move gizmo's x arrow, dragged further out along itself */
    t = (br_transform3d *)br_get(&d->world, cube, d->ids3.transform3d);
    ed_view3d_project(ed, br_v3(0.1f, 0.5f, 0), &tip);
    {
        br_vec2 dir = br_v2_norm(br_v2_sub(tip, s));
        br_vec2 grab = br_v2_add(s, br_v2_scale(dir, 40));
        inject_drag(e, grab, br_v2_add(grab, br_v2_scale(dir, 60)));
    }
    if (t->position.x < 0.3f || fabsf(t->position.y - 0.5f) > 1e-3f || fabsf(t->position.z) > 1e-3f)
        { printf("FAIL: 3D: dragging the x arrow moved the cube to (%.2f %.2f %.2f)\n", (double)t->position.x, (double)t->position.y, (double)t->position.z); ++failures; }
    br_image_save_bmp(&e->framebuffer, "editor_3d_moved.bmp");
    ed_doc_checkpoint(d);
    ed_cmd_undo(ed);
    ground = d->order[0];                                    /* (undo rebuilt the world: new handles) */
    cube = d->order[1];
    cam = d->order[2];
    t = (br_transform3d *)br_get(&d->world, cube, d->ids3.transform3d);
    if (!t || fabsf(t->position.x) > 1e-4f) { printf("FAIL: 3D: undo didn't bring the cube back\n"); ++failures; }
    /* the scale gizmo (R): the centre box dragged right grows it evenly */
    d->selected = cube;
    inject_key(BR_KEY_R);
    frames(e, 2);
    ed_view3d_project(ed, br_v3(0, 0.5f, 0), &s);
    inject_drag(e, s, br_v2_add(s, br_v2(50, 0)));
    t = (br_transform3d *)br_get(&d->world, cube, d->ids3.transform3d);
    if (ed->view.gizmo != ED_GIZMO_SCALE || t->scale.x < 1.3f || fabsf(t->scale.x - t->scale.z) > 1e-4f) { printf("FAIL: 3D: the scale gizmo (%.2f)\n", (double)t->scale.x); ++failures; }
    /* rotate (E): the y ring, dragged around: it turns about y only */
    inject_key(BR_KEY_E);
    frames(e, 2);
    {   /* find the y ring under the mouse (hovering lights it), straight below the centre */
        br_vec2 a = s;
        int k, found = 0;
        ed_view3d_project(ed, br_v3(0, 0.5f, 0), &s);
        for (k = 8; k < 140 && !found; k += 2) {
            a = br_v2(s.x, s.y + (float)k);
            inject_move(a.x, a.y);
            frames(e, 1);
            found = ed->view.hot_axis == 1;
        }
        if (found) inject_drag(e, a, br_v2_add(a, br_v2(40, 0)));
        t = (br_transform3d *)br_get(&d->world, cube, d->ids3.transform3d);
        if (!found || fabsf(t->rotation.y) < 5 || fabsf(t->rotation.x) > 0.5f || fabsf(t->rotation.z) > 0.5f)
            { printf("FAIL: 3D: the rotate gizmo (%d: %.1f %.1f %.1f)\n", found, (double)t->rotation.x, (double)t->rotation.y, (double)t->rotation.z); ++failures; }
    }
    inject_key(BR_KEY_W);
    /* the camera, picked by its marker */
    {
        br_vec3 p;
        br_world_transform3d(&d->world, cam, &p, NULL, NULL);
        if (ed_view3d_project(ed, p, &s)) { inject_click(s.x, s.y); frames(e, 2); }
        if (d->selected != cam) { printf("FAIL: 3D: clicking the camera's marker\n"); ++failures; }
    }
    /* look through it (C), and back */
    inject_key(BR_KEY_C);
    frames(e, 2);
    if (!ed->view.through_camera) { printf("FAIL: 3D: C didn't look through the scene camera\n"); ++failures; }
    br_image_save_bmp(&e->framebuffer, "editor_3d_camera.bmp");
    inject_key(BR_KEY_C);
    frames(e, 2);
    /* a model dropped in from the project */
    {
        char full[ED_PATH_MAX];
        const char obj[] = "v -0.5 0 0.5\nv 0.5 0 0.5\nv 0 1 0\nv 0 0 -0.5\nf 1 2 3\nf 2 4 3\nf 4 1 3\nf 1 4 2\n";
        ed_asset_path(ed, "pyramid3d.obj", full, sizeof(full));
        br_file_write_all(full, obj, sizeof(obj) - 1);
        if (!ed_assets_drop3d(ed, "pyramid3d.obj", br_v3(3, 0, 0), BR_ENTITY_NULL)) { printf("FAIL: 3D: dropping a model (%s)\n", ed->status); ++failures; }
        else if (((br_mesh3d *)br_get(&d->world, d->selected, d->ids3.mesh))->shape != BR_MESH_MODEL) { printf("FAIL: 3D: the model's Mesh\n"); ++failures; }
        if (ed_asset_kind_of("pyramid3d.obj", false) != ED_ASSET_MODEL) { printf("FAIL: .obj isn't a model asset\n"); ++failures; }
        ed_assets_select(ed, "pyramid3d.obj");
        frames(e, 2);
        br_image_save_bmp(&e->framebuffer, "editor_3d_model.bmp");
        ed_assets_select(ed, "");
        br_fs_remove(full);
    }
    /* play: a box falls onto the ground; Stop puts it back */
    falling = ed_new3d_at(ed, ED_NEW_CUBE, br_v3(-2, 4, 0));
    br_add(&d->world, falling, rb3, NULL);
    br_add(&d->world, falling, col3, NULL);
    ((br_collider3d *)br_add(&d->world, ground, col3, NULL))->size = br_v3(1, 0.01f, 1);
    ed_cmd_play(ed);
    frames(e, 120);
    t = (br_transform3d *)br_get(&d->world, falling, d->ids3.transform3d);
    if (!t || fabsf(t->position.y - 0.5f) > 0.05f) { printf("FAIL: 3D: the box didn't land while playing (y %.2f)\n", t ? (double)t->position.y : -1.0); ++failures; }
    br_image_save_bmp(&e->framebuffer, "editor_3d_play.bmp");
    ed_cmd_play(ed);
    frames(e, 2);
    t = (br_transform3d *)br_get(&d->world, d->order[d->count - 1], d->ids3.transform3d);
    if (!t || fabsf(t->position.y - 4) > 1e-4f) { printf("FAIL: 3D: Stop didn't put the box back\n"); ++failures; }
    /* saved and opened again: it comes back in 3D */
    snprintf(ed->path_field, sizeof(ed->path_field), "editor_3d.scene");
    ed_cmd_save(ed);
    ed_cmd_new(ed);
    ed_view_set_3d(ed, false);
    snprintf(ed->path_field, sizeof(ed->path_field), "editor_3d.scene");
    ed_cmd_open(ed);
    if (!ed->view.three_d || d->count != 6) { printf("FAIL: 3D: reopening (3D %d, %u entities)\n", ed->view.three_d, d->count); ++failures; }
    ed_cmd_new(ed);
    ed_view_set_3d(ed, false);
    frames(e, 2);
    return failures;
}

/* Selecting several: a box in the view, dragging them together, Shift-click in the
 * list, Delete and undo, and the 3D gizmo moving two cubes at once. */
static void inject_mod_key(br_key k, bool down, uint32_t mods) {
    br_event ev = br_event_key(down ? BR_EVENT_KEY_DOWN : BR_EVENT_KEY_UP, k, mods, false);
    br_headless_inject(&ev);
}

/* The debugger: a breakpoint stops the playing game's script, the Debug tab shows where,
 * and Continue / Stop go on (the hook answers by itself here: auto_frames). */
/* Prefabs that update: save one, place two, change one here, change the prefab: both follow, the change stays. */
static br_entity child_named(editor *ed, br_entity parent, const char *name) {
    br_entity kids[16];
    uint32_t n = br_children(&ed->doc.world, parent, kids, 16), i;
    char buf[64];
    for (i = 0; i < n; ++i) if (strcmp(ed_doc_name(&ed->doc, kids[i], buf, sizeof(buf)), name) == 0) return kids[i];
    return BR_ENTITY_NULL;
}

static int test_prefab_flow(br_engine *e) {
    editor *ed = &ED;
    ed_doc *d = &ed->doc;
    int failures = 0;
    br_entity lamp, glow, a = BR_ENTITY_NULL, b = BR_ENTITY_NULL;
    char rel[ED_PATH_MAX], full[ED_PATH_MAX * 2];
    uint32_t i, before;
    ed_doc_mark_clean(d);
    ed_cmd_new(ed);
    ed_assets_destroy(ed);
    ed_assets_init(ed, "editor_project");
    lamp = ed_doc_create(d, "Lamp", br_v2(100, 100));
    ((br_shape *)br_add(&d->world, lamp, d->ids.shape, NULL))->color = br_rgb(200, 200, 0);
    glow = ed_doc_create(d, "Glow", br_v2(100, 90));
    br_add(&d->world, glow, d->ids.shape, NULL);
    br_set_parent(&d->world, glow, lamp, true);
    ed_doc_normalize(d);
    d->selected = lamp;
    if (!ed_assets_save_prefab(ed) || !ed_doc_prefab_of(d, lamp)) { printf("FAIL: prefabs: saving one didn't make the entity its instance\n"); ++failures; return failures; }
    snprintf(rel, sizeof(rel), "%s", ed_doc_prefab_of(d, lamp));
    snprintf(full, sizeof(full), "%s%s", br_assets_root(), rel);
    before = d->count;
    if (ed_doc_instantiate(d, full, br_v2(200, 100)) < 0 || d->count != before + 2) { printf("FAIL: prefabs: placing one (%u -> %u)\n", before, d->count); ++failures; }
    for (i = 0; i < d->count; ++i)
        if (ed_doc_prefab_of(d, d->order[i])) { if (a == BR_ENTITY_NULL) a = d->order[i]; else b = d->order[i]; }
    if (a == BR_ENTITY_NULL || b == BR_ENTITY_NULL || child_named(ed, b, "Glow") == BR_ENTITY_NULL) { printf("FAIL: prefabs: two instances, with their parts\n"); ++failures; return failures; }
    ((br_shape *)br_get(&d->world, child_named(ed, b, "Glow"), d->ids.shape))->color = br_rgb(255, 0, 0);   /* an override, on B */
    frames(e, 2);
    {   /* the prefab file changes (as if edited elsewhere): both follow, B keeps its red glow */
        size_t len = 0;
        char *text = (char *)br_file_read_all(full, &len), *at;
        if (text && (at = strstr(text, "#c8c800ff")) != NULL) memcpy(at, "#00c8c8ff", 9);   /* the lamp: yellow -> teal */
        if (text) br_file_write_all(full, text, strlen(text));
        br_free(text);
    }
    if (!ed_doc_refresh_prefabs(d)) { printf("FAIL: prefabs: making instances again\n"); ++failures; }
    {
        int teal = 0, red = 0;
        for (i = 0; i < d->count; ++i) {
            const br_shape *sh = (const br_shape *)br_get(&d->world, d->order[i], d->ids.shape);
            if (sh && sh->color.r == 0 && sh->color.g == 200 && sh->color.b == 200) ++teal;
            if (sh && sh->color.r == 255 && sh->color.g == 0) ++red;
        }
        if (teal != 2 || red != 1) { printf("FAIL: prefabs: after the prefab changed: %d teal lamps, %d red glows\n", teal, red); ++failures; }
    }
    {   /* Apply: an instance's change written to the prefab, for every instance */
        br_entity first = BR_ENTITY_NULL;
        for (i = 0; i < d->count && first == BR_ENTITY_NULL; ++i) if (ed_doc_prefab_of(d, d->order[i])) first = d->order[i];
        ((br_shape *)br_get(&d->world, first, d->ids.shape))->size = br_v2(40, 40);
        if (!ed_doc_apply_prefab(d, first)) { printf("FAIL: prefabs: Apply\n"); ++failures; }
        {
            int big = 0;
            for (i = 0; i < d->count; ++i) {
                const br_shape *sh = (const br_shape *)br_get(&d->world, d->order[i], d->ids.shape);
                if (sh && ed_doc_prefab_of(d, d->order[i]) && sh->size.x == 40) ++big;
            }
            if (big != 2) { printf("FAIL: prefabs: after Apply, %d of 2 instances changed\n", big); ++failures; }
        }
    }
    frames(e, 2);
    br_image_save_bmp(&e->framebuffer, "editor_prefabs.bmp");
    ed_doc_mark_clean(d);
    return failures;
}

/* The game's own components: a script declares one, the editor's world has it, scenes keep it. */
static int test_own_components(br_engine *e) {
    editor *ed = &ED;
    ed_doc *d = &ed->doc;
    int failures = 0;
    br_entity gem;
    br_component gc;
    const char decl[] = "component Gem {\n    var value: int = 5\n    var colour: enum(red, green) = green\n}\n";
    ed_doc_mark_clean(d);
    ed_cmd_new(ed);
    br_fs_mkdir("editor_project/scripts");
    br_file_write_all("editor_project/scripts/parts.bs", decl, sizeof(decl) - 1);
    ed_assets_destroy(ed);
    ed_assets_init(ed, "editor_project");
    gem = ed_doc_create(d, "Gem", br_v2(50, 50));
    ed_components_reload(ed, true);
    gc = br_component_find(&d->world, "Gem");
    if (gc == BR_COMPONENT_INVALID) { printf("FAIL: own components: the editor's world doesn't have Gem\n"); return failures + 1; }
    gem = d->count ? d->order[0] : BR_ENTITY_NULL;                  /* (the world was made again: the entity is new) */
    if (!br_entity_alive(&d->world, gem) || !br_add(&d->world, gem, gc, NULL)) { printf("FAIL: own components: adding Gem\n"); ++failures; }
    d->selected = gem;
    frames(e, 2);
    {   /* in the scene's text, and changed declarations keep the values */
        const char decl2[] = "component Gem {\n    var value: int = 5\n    var colour: enum(red, green) = green\n    var shine: number = 1\n}\n";
        int *value = (int *)br_get(&d->world, gem, gc);
        if (value) *value = 9;
        br_file_write_all("editor_project/scripts/parts.bs", decl2, sizeof(decl2) - 1);
        ed_components_reload(ed, true);
        gc = br_component_find(&d->world, "Gem");
        gem = d->count ? d->order[0] : BR_ENTITY_NULL;
        if (gc == BR_COMPONENT_INVALID || br_component_get_desc(&d->world, gc)->field_count != 3 || !br_get(&d->world, gem, gc) || *(const int *)br_get(&d->world, gem, gc) != 9)
            { printf("FAIL: own components: a changed declaration lost the scene's values\n"); ++failures; }
    }
    br_fs_remove("editor_project/scripts/parts.bs");
    ed_components_reload(ed, true);
    ed_doc_mark_clean(d);
    ed_cmd_new(ed);
    return failures;
}

static void inject_key_mods(br_key key, uint32_t mods) {      /* (the up keeps the modifiers: they're read after) */
    br_event k = br_event_key(BR_EVENT_KEY_DOWN, key, mods, false);
    br_headless_inject(&k);
    k = br_event_key(BR_EVENT_KEY_UP, key, mods, false);
    br_headless_inject(&k);
}

static void inject_text(br_engine *e, const char *text) {
    for (; *text; ++text) {
        br_event t = br_event_make(BR_EVENT_TEXT_INPUT);
        t.u.text.codepoint = (unsigned char)*text;
        br_headless_inject(&t);
        frames(e, 1);
    }
}

static void key_frame(br_engine *e, br_key key, uint32_t mods) {
    br_event up = br_event_key(BR_EVENT_KEY_UP, key, 0, false);
    inject_key_mods(key, mods);
    frames(e, 1);
    br_headless_inject(&up);                                       /* the modifiers let go */
}

/* The Scripts tab's editor: typing, indenting, undo, saving, mistakes, breakpoints that move. */
static int test_code_editor(br_engine *e) {
    editor *ed = &ED;
    int failures = 0;
    ed_code_file *f;
    char *disk;
    size_t size;
    const char start[] = "func update(dt) {\n}\n";
    br_fs_mkdir("editor_project/scripts");
    br_file_write_all("editor_project/scripts/typed.bs", start, sizeof(start) - 1);
    ed_code_close_all(ed);
    ed_assets_destroy(ed);
    ed_assets_init(ed, "editor_project");
    if (!ed_code_open(ed, "scripts/typed.bs", 1) || !(f = ed_code_current(ed))) { printf("FAIL: code: opening a script\n"); return 1; }
    frames(e, 2);
    ed->code.focused = true;
    f->cursor = f->anchor = 17;                                     /* after the '{' */
    key_frame(e, BR_KEY_ENTER, 0);
    inject_text(e, "self.x += 1");
    if (strcmp(f->text, "func update(dt) {\n    self.x += 1\n}\n") != 0 || !f->dirty) { printf("FAIL: code: typing (%s)\n", f->text); ++failures; }
    key_frame(e, BR_KEY_Z, BR_MOD_CONTROL);                         /* the typing, then the new line */
    if (strcmp(f->text, "func update(dt) {\n    \n}\n") != 0) { printf("FAIL: code: undo (%s)\n", f->text); ++failures; }
    key_frame(e, BR_KEY_Z, BR_MOD_CONTROL);
    if (strcmp(f->text, start) != 0) { printf("FAIL: code: undo twice (%s)\n", f->text); ++failures; }
    key_frame(e, BR_KEY_Z, BR_MOD_CONTROL | BR_MOD_SHIFT);
    key_frame(e, BR_KEY_Y, BR_MOD_CONTROL);
    if (strcmp(f->text, "func update(dt) {\n    self.x += 1\n}\n") != 0) { printf("FAIL: code: redo (%s)\n", f->text); ++failures; }
    key_frame(e, BR_KEY_S, BR_MOD_CONTROL);
    disk = (char *)br_file_read_all("editor_project/scripts/typed.bs", &size);
    if (f->dirty || !disk || size != (size_t)f->len || memcmp(disk, f->text, size) != 0) { printf("FAIL: code: Ctrl+S\n"); ++failures; }
    br_free(disk);
    if (ed_doc_dirty(&ed->doc) && strstr(ed->status, "scene")) { printf("FAIL: code: Ctrl+S saved the scene too\n"); ++failures; }
    {   /* a mistake is found as you type, and goes when it's mended */
        inject_text(e, " +");
        frames(e, 40);
        if (f->error_line != 2 || !f->error[0]) { printf("FAIL: code: the mistake on line 2 wasn't found (%d: %s)\n", f->error_line, f->error); ++failures; }
        br_image_save_bmp(&e->framebuffer, "editor_code.bmp");
        key_frame(e, BR_KEY_BACKSPACE, 0);
        key_frame(e, BR_KEY_BACKSPACE, 0);
        frames(e, 40);
        if (f->error_line != 0) { printf("FAIL: code: the mistake stayed (%s)\n", f->error); ++failures; }
    }
    {   /* breakpoints move with their lines */
        br_script_debug_set_breakpoint(f->path, 3, true);
        f->cursor = f->anchor = 0;
        key_frame(e, BR_KEY_ENTER, 0);                                  /* a line above */
        if (!br_script_debug_has_breakpoint(f->path, 4) || br_script_debug_has_breakpoint(f->path, 3)) { printf("FAIL: code: a breakpoint didn't move down\n"); ++failures; }
        key_frame(e, BR_KEY_Z, BR_MOD_CONTROL);
        if (!br_script_debug_has_breakpoint(f->path, 3)) { printf("FAIL: code: a breakpoint didn't move back with undo\n"); ++failures; }
        br_script_debug_clear_breakpoints();
    }
    {   /* indenting lines, commenting them out, a '}' that steps back */
        f->anchor = 0;
        f->cursor = f->len;
        key_frame(e, BR_KEY_TAB, 0);
        if (strncmp(f->text, "    func update(dt) {\n        self.x", 36) != 0) { printf("FAIL: code: Tab on lines (%s)\n", f->text); ++failures; }
        key_frame(e, BR_KEY_TAB, BR_MOD_SHIFT);
        key_frame(e, BR_KEY_SLASH, BR_MOD_CONTROL);
        if (strncmp(f->text, "# func update(dt) {\n    # self.x += 1\n# }", 40) != 0) { printf("FAIL: code: Ctrl+/ (%s)\n", f->text); ++failures; }
        key_frame(e, BR_KEY_SLASH, BR_MOD_CONTROL);
        if (strcmp(f->text, "func update(dt) {\n    self.x += 1\n}\n") != 0) { printf("FAIL: code: Ctrl+/ again (%s)\n", f->text); ++failures; }
        f->cursor = f->anchor = 33;                                     /* the end of line 2 */
        key_frame(e, BR_KEY_ENTER, 0);
        inject_text(e, "}");
        if (strcmp(f->text, "func update(dt) {\n    self.x += 1\n}\n}\n") != 0) { printf("FAIL: code: '}' on an empty line (%s)\n", f->text); ++failures; }
        key_frame(e, BR_KEY_Z, BR_MOD_CONTROL);
        key_frame(e, BR_KEY_Z, BR_MOD_CONTROL);
    }
    {   /* Play saves it; while playing it's read-only */
        f->cursor = f->anchor = f->len;
        inject_text(e, "# end\n");
        ed_cmd_play(ed);
        frames(e, 2);
        disk = (char *)br_file_read_all("editor_project/scripts/typed.bs", &size);
        if (f->dirty || !disk || !strstr(disk, "# end")) { printf("FAIL: code: Play didn't save the script\n"); ++failures; }
        br_free(disk);
        ed->code.focused = true;
        inject_text(e, "zz");
        if (strstr(f->text, "zz")) { printf("FAIL: code: typing while playing changed the script\n"); ++failures; }
        ed_cmd_play(ed);
        frames(e, 2);
    }
    {   /* changed by another program: read again */
        const char other[] = "var changed = 1\n";
        br_file_write_all("editor_project/scripts/typed.bs", other, sizeof(other) - 1);
        f->modified = 1;
        frames(e, 70);
        if (strcmp(f->text, other) != 0) { printf("FAIL: code: a script changed elsewhere wasn't read again\n"); ++failures; }
    }
    {   /* closing one with changes asks first */
        ed->code.focused = true;
        inject_text(e, "x");
        if (ed_code_close(ed, ed->code.current, false) || !ed_code_close(ed, ed->code.current, false) || ed_code_find(ed, "scripts/typed.bs") >= 0)
            { printf("FAIL: code: closing a changed script (%s)\n", ed->status); ++failures; }
    }
    br_fs_remove("editor_project/scripts/typed.bs");
    ed->bottom_tab = ED_TAB_FILES;
    frames(e, 2);
    return failures;
}

static int test_debugger(br_engine *e) {
    editor *ed = &ED;
    int failures = 0;
    br_entity hero;
    const char mover[] = "var steps = 0\nfunc update(dt) {\n    steps += 1\n    var far = self.x + 2\n    self.x = far\n}\n";
    br_fs_mkdir("editor_project/scripts");
    br_file_write_all("editor_project/scripts/stepper.bs", mover, sizeof(mover) - 1);
    ed_doc_mark_clean(&ed->doc);
    ed_cmd_new(ed);
    ed_assets_destroy(ed);
    ed_assets_init(ed, "editor_project");
    hero = ed_doc_create(&ed->doc, "Stepper", br_v2(100, 90));
    snprintf(((br_script *)br_add(&ed->doc.world, hero, br_component_find(&ed->doc.world, "Script"), NULL))->file, 96, "scripts/stepper.bs");
    frames(e, 2);
    ed->bottom_tab = ED_TAB_DEBUG;
    ed_panel_set_open(ed, ED_PANEL_BOTTOM, true);
    if (!ed_debug_show_file(ed, "scripts/stepper.bs", 0) || !ed_code_current(ed) || ed_code_current(ed)->line_count != 7)
        { printf("FAIL: the debugger didn't show the script (%d lines)\n", ed_code_current(ed) ? ed_code_current(ed)->line_count : -1); ++failures; }
    if (!ed_debug_toggle_breakpoint(ed, 4)) { printf("FAIL: setting a breakpoint\n"); ++failures; }
    frames(e, 2);
    ed->dbg.auto_frames = 3;
    ed->dbg.auto_action = BR_DEBUG_CONTINUE;
    ed->dbg.auto_shot = "editor_debugger.bmp";
    ed_cmd_play(ed);
    frames(e, 4);
    if (ed->dbg.stops < 2 || ed->dbg.last_line != 4) { printf("FAIL: the breakpoint didn't stop the script (%d stops, line %d)\n", ed->dbg.stops, ed->dbg.last_line); ++failures; }
    if (ed->dbg.nlocals < 1 || strcmp(ed->dbg.locals[0].name, "dt") != 0) { printf("FAIL: the debugger didn't see update()'s variables\n"); ++failures; }
    if (ed->dbg.nglobals < 1 || strcmp(ed->dbg.globals[0].name, "steps") != 0) { printf("FAIL: the debugger didn't see the script's variables\n"); ++failures; }
    if (ed->dbg.stopped) { printf("FAIL: still stopped after Continue\n"); ++failures; }
    ed->dbg.auto_action = BR_DEBUG_DETACH;                        /* Stop: the game stops playing */
    frames(e, 3);
    if (ed->doc.playing) { printf("FAIL: Stop in the debugger didn't stop playing\n"); ++failures; }
    ed_debug_toggle_breakpoint(ed, 4);
    if (br_script_debug_breakpoint(-1, NULL, NULL) != 0) { printf("FAIL: clearing the breakpoint\n"); ++failures; }
    ed->dbg.auto_frames = 0;
    ed->bottom_tab = ED_TAB_FILES;
    frames(e, 2);
    return failures;
}

static int test_multi_select(br_engine *e) {
    editor *ed = &ED;
    ed_doc *d = &ed->doc;
    int failures = 0;
    br_vec2 a, b, title0, ball0;
    br_entity title, ball, sky;
    uint32_t count;
    ed_sample_scene(d);
    ed_view_set_3d(ed, false);
    ed->view.zoom = 2.0f;
    ed->view.center = br_v2(160, 90);
    frames(e, 2);
    sky = d->order[0];
    title = d->order[2];
    ball = d->order[3];
    title0 = br_world_transform(&d->world, title).position;
    ball0 = br_world_transform(&d->world, ball).position;
    a = ed_view_to_screen(&ed->view, br_v2(-20, -20));          /* outside the game's screen: empty */
    b = ed_view_to_screen(&ed->view, br_v2(170, 130));
    inject_drag(e, a, b);
    count = ed_doc_selection_count(d);
    if (count != 4 || !ed_doc_is_selected(d, title) || !ed_doc_is_selected(d, ball) || !ed_doc_is_selected(d, sky))
        { printf("FAIL: the box selected %u entities\n", (unsigned)count); ++failures; }
    br_image_save_bmp(&e->framebuffer, "editor_multi.bmp");
    a = ed_view_to_screen(&ed->view, ball0);                    /* drag Ball: all four move */
    inject_drag(e, a, br_v2_add(a, br_v2(20 * ed->view.zoom, 0)));
    if (fabsf(br_world_transform(&d->world, ball).position.x - ball0.x - 20) > 0.5f ||
        fabsf(br_world_transform(&d->world, title).position.x - title0.x - 20) > 0.5f)
        { printf("FAIL: dragging one of the selection didn't move the rest\n"); ++failures; }
    if (ed_doc_selection_count(d) != 4) { printf("FAIL: dragging lost the selection\n"); ++failures; }
    ed_cmd_undo(ed);
    frames(e, 2);
    /* the list: click row 1, Shift-click row 4: rows 1 to 4 */
    inject_click(60, HIER_ROW_Y(1));
    frames(e, 2);
    inject_mod_key(BR_KEY_LEFT_SHIFT, true, BR_MOD_SHIFT);
    inject_click(60, HIER_ROW_Y(4));
    frames(e, 2);
    inject_mod_key(BR_KEY_LEFT_SHIFT, false, 0);
    frames(e, 1);
    if (ed_doc_selection_count(d) != 4 || !ed_doc_is_selected(d, d->order[1]) || !ed_doc_is_selected(d, d->order[4]))
        { printf("FAIL: Shift-click in the list selected %u\n", (unsigned)ed_doc_selection_count(d)); ++failures; }
    inject_mod_key(BR_KEY_LEFT_CONTROL, true, BR_MOD_CONTROL);   /* Ctrl-click row 2: out */
    inject_click(60, HIER_ROW_Y(2));
    frames(e, 2);
    inject_mod_key(BR_KEY_LEFT_CONTROL, false, 0);
    frames(e, 1);
    if (ed_doc_selection_count(d) != 3 || ed_doc_is_selected(d, d->order[2])) { printf("FAIL: Ctrl-click in the list\n"); ++failures; }
    br_image_save_bmp(&e->framebuffer, "editor_multi_list.bmp");
    count = d->count;
    ed_cmd_delete(ed);
    if (d->count != count - 3) { printf("FAIL: Delete took %u, not 3\n", (unsigned)(count - d->count)); ++failures; }
    ed_doc_checkpoint(d);
    ed_cmd_undo(ed);
    if (d->count != count) { printf("FAIL: undoing a multiple delete\n"); ++failures; }
    /* 3D: two cubes, the move gizmo's x arrow moves both */
    {
        br_entity c1, c2;
        br_vec2 s, tip, dir;
        const br_transform3d *t1, *t2;
        ed_doc_mark_clean(d);                                /* (no "save changes?" question) */
        ed_cmd_new(ed);
        ed_view_set_3d(ed, true);
        ed_view3d_reset(&ed->view);
        ed->view.gizmo = ED_GIZMO_MOVE;
        c1 = ed_new3d_at(ed, ED_NEW_CUBE, br_v3(0, 0, 0));
        c2 = ed_new3d_at(ed, ED_NEW_CUBE, br_v3(-3, 0, 2));
        ed_doc_select(d, c2);
        ed_doc_select_add(d, c1);
        frames(e, 2);
        ed_view3d_project(ed, br_v3(0, 0.5f, 0), &s);
        ed_view3d_project(ed, br_v3(0.1f, 0.5f, 0), &tip);
        dir = br_v2_norm(br_v2_sub(tip, s));
        inject_drag(e, br_v2_add(s, br_v2_scale(dir, 40)), br_v2_add(s, br_v2_scale(dir, 100)));
        t1 = (const br_transform3d *)br_get(&d->world, c1, d->ids3.transform3d);
        t2 = (const br_transform3d *)br_get(&d->world, c2, d->ids3.transform3d);
        if (t1->position.x < 0.3f || fabsf((t2->position.x + 3) - t1->position.x) > 1e-3f || fabsf(t2->position.z - 2) > 1e-3f)
            { printf("FAIL: 3D: the gizmo didn't move both cubes (%.2f, %.2f)\n", (double)t1->position.x, (double)t2->position.x); ++failures; }
        br_image_save_bmp(&e->framebuffer, "editor_multi_3d.bmp");
        ed_doc_mark_clean(d);                                /* (no "save changes?" question) */
        ed_cmd_new(ed);
        ed_view_set_3d(ed, false);
    }
    frames(e, 2);
    return failures;
}

/* ---- --build: the Build menu from the command line ------------------------------------ */

static int run_build(br_engine *e, ed_target t) {
    editor *ed = &ED;
    char out[ED_PATH_MAX];
    frames(e, 1);                                          /* (the editor scene opens the project) */
    if (!ed_build(ed, t, out, sizeof(out))) {             /* (the problem is on stderr already; this sums it up) */
        const char *why = ed->status;
        char *console = strstr(ed->status, " - see the console");
        if (console) *console = 0;
        if (strncmp(why, "Build: ", 7) == 0) why += 7;
        fflush(stdout);
        fprintf(stderr, "editor --build: %s\n", why[0] ? why : "the build failed");
        return 1;
    }
    printf("%s\n", ed->status);
    return 0;
}

static int run_headless(br_engine *e) {
    editor *ed = &ED;
    int failures = 0;
    br_vec2 start, after;
    const br_transform *t;

    frames(e, 3);
    {
        const br_image *icon = br_headless_icon();
        if (!icon || icon->width != 256 || icon->pixels[128 * 256 + 128].a == 0 || icon->pixels[0].a != 0)
            { printf("FAIL: the window icon wasn't set\n"); ++failures; }
    }
    /* click "Ball" in the hierarchy: heading + a button row + separator, then rows of 19 px */
    inject_click(60, HIER_ROW_Y(3));
    frames(e, 2);
    t = (const br_transform *)br_get(&ed->doc.world, ed->doc.selected, ed->doc.ids.transform);
    if (!t || ed_doc_index(&ed->doc, ed->doc.selected) != 3) { printf("FAIL: clicking the hierarchy didn't select Ball\n"); ++failures; }
    br_image_save_bmp(&e->framebuffer, "editor.bmp");
    start = t ? t->position : br_v2(0, 0);
    {   /* drag Ball to the top of the list, and back */
        br_entity ball = ed->doc.selected;
        inject_drag(e, br_v2(60, HIER_ROW_Y(3)), br_v2(60, HIER_ROW_Y(0) - 6));
        if (ed_doc_index(&ed->doc, ball) != 0) { printf("FAIL: dragging in the hierarchy didn't reorder (at %d)\n", ed_doc_index(&ed->doc, ball)); ++failures; }
        inject_drag(e, br_v2(60, HIER_ROW_Y(0)), br_v2(60, HIER_ROW_Y(3) + 4));
        if (ed_doc_index(&ed->doc, ball) != 3) { printf("FAIL: dragging back put Ball at %d\n", ed_doc_index(&ed->doc, ball)); ++failures; }
    }
    {   /* the filter: "#bouncy" shows only Ball (tagged in its inspector), as the first row - a click selects it */
        br_entity ball = ed->doc.order[3];
        ed->doc.selected = ed->doc.order[0];
        br_tag_add(&ed->doc.world, ball, "bouncy");
        snprintf(ed->hierarchy_filter, sizeof(ed->hierarchy_filter), "#bouncy");
        frames(e, 2);
        br_image_save_bmp(&e->framebuffer, "editor_filter.bmp");
        inject_click(60, HIER_ROW_Y(1));                 /* (row 0 is "1 of N match") */
        frames(e, 2);
        if (ed->doc.selected != ball) { printf("FAIL: the hierarchy's #tag filter didn't show Ball first\n"); ++failures; }
        ed->hierarchy_filter[0] = 0;
        br_tag_remove(&ed->doc.world, ball, "bouncy");
        br_remove(&ed->doc.world, ball, ed->doc.ids.tags);
        frames(e, 2);
    }
    {   /* the tree: drag row 4 onto the middle of row 3 (Ball) - it becomes Ball's child and stays put */
        br_entity ball = ed->doc.order[3], other = ed->doc.order[4];
        br_vec2 before = br_world_transform(&ed->doc.world, other).position, moved;
        inject_drag(e, br_v2(60, HIER_ROW_Y(4)), br_v2(80, HIER_ROW_Y(3)));
        moved = br_world_transform(&ed->doc.world, other).position;
        if (br_parent_of(&ed->doc.world, other) != ball || ed->doc.order[4] != other || br_v2_dist(before, moved) > 0.01f)
            { printf("FAIL: dropping a row onto another didn't make it a child\n"); ++failures; }
        ed->doc.selected = other;
        ed_cmd_add_child(ed);                                 /* a grandchild */
        if (br_depth(&ed->doc.world, ed->doc.selected) != 2) { printf("FAIL: + Child\n"); ++failures; }
        frames(e, 2);
        br_image_save_bmp(&e->framebuffer, "editor_tree.bmp");
        inject_click(6 + 6, HIER_ROW_Y(3));              /* fold Ball's arrow: its subtree hides */
        frames(e, 2);
        if (!ed_collapsed(ed, ball)) { printf("FAIL: clicking the arrow didn't fold the row\n"); ++failures; }
        ed_set_collapsed(ed, ball, false);
        ed_cmd_undo(ed); ed_cmd_undo(ed);                       /* back to the flat sample scene */
        frames(e, 2);
        if (ed->doc.count != 7 || br_parent_of(&ed->doc.world, ed->doc.order[4]) != BR_ENTITY_NULL)
            { printf("FAIL: undoing the tree changes (%u entities)\n", ed->doc.count); ++failures; }
        ed->doc.selected = ed->doc.order[3];
    }

    ed_cmd_play(ed);
    frames(e, 60);
    t = (const br_transform *)br_get(&ed->doc.world, ed->doc.order[3], ed->doc.ids.transform);
    after = t ? t->position : start;
    if (br_v2_dist(start, after) < 10) { printf("FAIL: Ball didn't move while playing\n"); ++failures; }
    br_image_save_bmp(&e->framebuffer, "editor_play.bmp");
    ed_cmd_play(ed);                                         /* stop */
    frames(e, 2);
    t = (const br_transform *)br_get(&ed->doc.world, ed->doc.order[3], ed->doc.ids.transform);
    if (!t || t->position.x != start.x || t->position.y != start.y) { printf("FAIL: Stop didn't restore the scene\n"); ++failures; }

    ed_cmd_nudge(ed, br_v2(5, 0));                           /* edit, undo, redo */
    frames(e, 2);
    ed_cmd_undo(ed);
    t = (const br_transform *)br_get(&ed->doc.world, ed->doc.selected, ed->doc.ids.transform);
    if (!t || t->position.x != start.x) { printf("FAIL: undo\n"); ++failures; }

    snprintf(ed->path_field, sizeof(ed->path_field), "editor_demo");
    ed_cmd_save(ed);
    if (strcmp(ed->doc.path, "editor_demo.scene") != 0 || ed_doc_dirty(&ed->doc)) { printf("FAIL: save\n"); ++failures; }
    ed_cmd_new(ed);
    snprintf(ed->path_field, sizeof(ed->path_field), "editor_demo.scene");
    ed_cmd_open(ed);
    if (ed->doc.count != 7) { printf("FAIL: reopening gave %u entities\n", ed->doc.count); ++failures; }
    frames(e, 2);
    br_image_save_bmp(&e->framebuffer, "editor_reopened.bmp");
    failures += test_menus_console(e);
    failures += test_assets(e);
    failures += test_panels(e);
    failures += test_view3d(e);
    failures += test_multi_select(e);
    failures += test_debugger(e);
    failures += test_prefab_flow(e);
    failures += test_own_components(e);
    failures += test_code_editor(e);
    failures += test_build(e);
    printf("editor (headless): %s - wrote editor*.bmp, editor_demo.scene, editor_project/\n",
           failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}

/* ---- main --------------------------------------------------------------------------- */

static void script_print(const char *text, void *user) {   /* print() in a script: the console and the status bar */
    BR_UNUSED(user);
    ed_console_add(&ED.console, ED_CON_PRINT, text, ED.engine ? ED.engine->time : 0);
    br_capture_write_original(BR_STREAM_STDOUT, text, strlen(text));     /* and the terminal */
    br_capture_write_original(BR_STREAM_STDOUT, "\n", 1);
    ed_status(&ED, "print: %s", text);
}

static bool on_init(br_engine *e) {
    br_script_set_print(script_print, NULL);
    br_image icon;
    ed_make_icon(&icon, 256);                    /* window, taskbar and Dock icon */
    br_window_set_icon(e, &icon);
    br_image_downscale(&icon, &ED.badge, 24, 24);
    br_image_destroy(&icon);
    e->window.pixel_scale = (int)(br_window_scale(e) + 0.5f);   /* HiDPI: lay out in points */
    if (e->window.pixel_scale < 1) e->window.pixel_scale = 1;
    br_engine_sync_window(e);
    return true;
}

int main(int argc, char **argv) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config cfg = br_engine_default_config();
    int i, rc = 0;
    bool build = false;
    ed_target target = ED_TARGET_LINUX;
    if (argc >= 3 && strcmp(argv[1], "--write-icon") == 0) {    /* the icon as a PNG, then done */
        br_image icon;
        int size = argc > 3 ? atoi(argv[3]) : 256;
        bool ok;
        if (size < 16 || size > 1024) size = 256;
        ed_make_icon(&icon, size);
        ok = br_image_save_png(&icon, argv[2]);
        br_image_destroy(&icon);
        if (!ok) fprintf(stderr, "editor: can't write %s\n", argv[2]);
        return ok ? 0 : 1;
    }
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) ED.headless = true;
        else if (strcmp(argv[i], "--build") == 0) {
            if (i + 1 >= argc || !ed_target_from_name(argv[i + 1], &target)) {
                fprintf(stderr, "usage: editor --build linux|macos|windows|web|host [project folder] [--out folder]\n");
                br_free(e);
                return 2;
            }
            build = ED.build_cli = true;
            ++i;
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            snprintf(ED.build_out, sizeof(ED.build_out), "%s", argv[++i]);
        } else g_open_path = argv[i];
    }
    if (build) {
        setvbuf(stdout, NULL, _IOLBF, 0);                  /* (in order with stderr's lines) */
        if (!g_open_path) g_open_path = ".";
        if (!br_fs_is_dir(g_open_path)) {
            fprintf(stderr, "editor --build: %s isn't a project folder\n", g_open_path);
            br_free(e);
            return 2;
        }
    }
    cfg.title = "Bronto Editor";
    cfg.width = 1280;
    cfg.height = 720;
    cfg.window_scale = 1;
    cfg.pixel_scale = 1;
    cfg.scale_mode = BR_SCALE_RESIZE;           /* the UI fills the window at its native size */
    cfg.min_window_width = 900;
    cfg.min_window_height = 540;
    cfg.clear_color = br_rgb(30, 34, 40);
    cfg.initial_scene = &k_editor;
    cfg.on_init = on_init;
    if (ED.headless || build) cfg.platform = br_platform_headless();
    if (!build) ED.console_capturing = br_capture_start(true);   /* the Console tab; the terminal still gets it all */
    if (!br_engine_init(e, &cfg)) { br_capture_stop(); br_free(e); return 1; }

    if (build) rc = run_build(e, target);
    else if (ED.headless) rc = run_headless(e);
    else br_engine_loop(e, NULL);       /* the editor scene does the rest */

    br_engine_shutdown(e);
    br_capture_stop();
    br_free(e);
    return rc;
}
