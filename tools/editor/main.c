/*
 * BRONTO EDITOR - entry point: engine setup, window layout and shortcuts.
 *
 *   editor [scene file]        open a scene (or start with a sample one)
 *   editor [project folder]    open a project (its main.scene, if it has one)
 *   editor --headless          scripted self-test with screenshots
 *
 * Shortcuts (Ctrl, or Cmd on a Mac):
 *   Ctrl+Shift+O load project   Ctrl+N new   Ctrl+O open   Ctrl+S save   Ctrl+Z undo   Ctrl+Y / Ctrl+Shift+Z redo
 *   Ctrl+D duplicate   Delete remove   F5 / Ctrl+P play / stop   F frame selection
 *   G grid   arrows nudge (Shift: one grid step)   Esc deselect / stop
 *
 * The editor is one engine scene. Its own UI is drawn with br_ui; the
 * edited scene is a separate ECS world owned by the document (doc.c).
 */
#include "editor.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TOOLBAR_H 28.0f
#define STATUS_H 20.0f
#define HIERARCHY_W 200.0f
#define INSPECTOR_W 300.0f
#define ASSETS_H 196.0f
#define PREVIEW_W 230.0f

static editor ED;
static const char *g_open_path;

/* ---- layout ---------------------------------------------------------------- */

typedef struct layout { br_rect screen, toolbar, hierarchy, view, assets, preview, inspector, status; } layout;

static layout make_layout(const br_engine *e) {
    layout l;
    float w = (float)e->framebuffer.width, h = (float)e->framebuffer.height, mid = h - TOOLBAR_H - STATUS_H;
    float centre_w = w - HIERARCHY_W - INSPECTOR_W, view_h = mid - ASSETS_H;
    l.screen = br_rect_make(0, 0, w, h);
    l.toolbar = br_rect_make(0, 0, w, TOOLBAR_H);
    l.hierarchy = br_rect_make(0, TOOLBAR_H, HIERARCHY_W, mid);
    l.inspector = br_rect_make(w - INSPECTOR_W, TOOLBAR_H, INSPECTOR_W, mid);
    l.view = br_rect_make(HIERARCHY_W + 1, TOOLBAR_H + 1, centre_w - 2, view_h - 2);
    l.assets = br_rect_make(HIERARCHY_W, TOOLBAR_H + view_h, centre_w - PREVIEW_W - 1, ASSETS_H);
    l.preview = br_rect_make(HIERARCHY_W + centre_w - PREVIEW_W, TOOLBAR_H + view_h, PREVIEW_W, ASSETS_H);
    l.status = br_rect_make(0, h - STATUS_H, w, STATUS_H);
    return l;
}

/* ---- shortcuts ---------------------------------------------------------------- */

static void shortcuts(editor *ed) {
    const br_input *in = &ed->engine->input;
    bool cmd = (in->mods & (BR_MOD_CONTROL | BR_MOD_SUPER)) != 0, shift = (in->mods & BR_MOD_SHIFT) != 0;
    float step;
    if (br_ui_wants_keyboard(&ed->ui) || ed->pending != ED_PENDING_NONE || ed->picker.open) return;   /* typing, or a dialog */
    if (ed->doc.playing) {                         /* the game has the keyboard: only Stop */
        if (br_key_pressed(in, BR_KEY_F5) || br_key_pressed(in, BR_KEY_ESCAPE) || (cmd && br_key_pressed(in, BR_KEY_P))) ed_cmd_play(ed);
        return;
    }
    if (cmd && br_key_pressed(in, BR_KEY_N)) ed_cmd_new(ed);
    if (cmd && br_key_pressed(in, BR_KEY_O)) { if (shift) ed_cmd_load_project(ed); else ed_cmd_open(ed); }
    if (cmd && br_key_pressed(in, BR_KEY_S)) ed_cmd_save(ed);
    if (cmd && br_key_pressed(in, BR_KEY_Z)) { if (shift) ed_cmd_redo(ed); else ed_cmd_undo(ed); }
    if (cmd && br_key_pressed(in, BR_KEY_Y)) ed_cmd_redo(ed);
    if (cmd && br_key_pressed(in, BR_KEY_D)) ed_cmd_duplicate(ed);
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

static bool editor_enter(br_scene *s) {
    editor *ed = &ED;
    ed->engine = s->engine;
    br_ui_init(&ed->ui);
    ed_view_init(&ed->view);
    ed->show_colliders = true;
    ed_doc_init(&ed->doc);
    ed->doc.world.context = ed->engine;              /* Sound components play while playing */
    {   /* the project: the scene's (see br_assets_find_root), or the folder the editor started in */
        char root[ED_PATH_MAX] = "";
        if (g_open_path && !br_fs_is_dir(g_open_path)) br_assets_find_root(g_open_path, root, sizeof(root));
        ed_assets_init(ed, root);
    }
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
    ed_cmd_frame(ed);
    return true;
}

static void editor_exit(br_scene *s) {
    BR_UNUSED(s);
    ed_assets_destroy(&ED);
    ed_picker_close(&ED);
    ed_doc_destroy(&ED.doc);
    br_image_destroy(&ED.badge);
}

static void editor_fixed(br_scene *s, float dt) {
    BR_UNUSED(s);
    ed_doc_step(&ED.doc, dt);                       /* the scene's systems run only while playing */
}

static void editor_update(br_scene *s, float dt) {
    editor *ed = &ED;
    BR_UNUSED(s);
    BR_UNUSED(dt);
    /* Between interactions, record an undo step if anything changed. */
    if (ed->ui.active == 0 && !br_ui_wants_keyboard(&ed->ui) && ed->view.drag == ED_DRAG_NONE)
        ed_doc_checkpoint(&ed->doc);
    shortcuts(ed);
    update_title(ed);
    if (ed->doc.playing && br_script_error_count(&ed->doc.world) != ed->script_errors_seen) {   /* a script broke */
        ed->script_errors_seen = br_script_error_count(&ed->doc.world);
        ed_status(ed, "Script error: %s", br_script_last_error(&ed->doc.world));
    }
}

/* All drawing (and the widgets' input) happens here, after the frame is cleared. */
static void editor_render(br_scene *s) {
    editor *ed = &ED;
    br_engine *e = s->engine;
    layout l = make_layout(e);
    br_ui_begin(&ed->ui, &e->renderer, &e->input, e->time);
    br_ui_set_disabled(&ed->ui, ed->pending != ED_PENDING_NONE || ed->picker.open);
    ed_view_frame(ed, l.view);
    ed_toolbar(ed, l.toolbar);
    ed_hierarchy(ed, l.hierarchy);
    ed_inspector(ed, l.inspector);
    ed_assets_panel(ed, l.assets);
    ed_assets_preview(ed, l.preview);
    ed_statusbar(ed, l.status);
    ed_dialog(ed, l.screen);
    ed_picker_dialog(ed, l.screen);
    br_ui_set_disabled(&ed->ui, false);
    br_ui_end(&ed->ui);
}

static bool editor_event(br_scene *s, const br_event *ev) {
    BR_UNUSED(s);
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

#define HIER_ROW_Y(i) (TOOLBAR_H + 6 + 19 * 2 + 5 + 3 + 19 * (float)(i) + 8)

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

/* The middle of tile `i` in the asset browser (toolbar row, breadcrumb row, then tiles). */
static br_vec2 tile_center(const br_engine *e, int i) {
    layout l = make_layout(e);
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
    if (ed->assets.list.count < 2 || strcmp(ed->assets.list.entries[0].name, "art") != 0 || strcmp(ed->assets.list.entries[1].name, "sfx") != 0)
        { printf("FAIL: the project doesn't list art/ and sfx/ first\n"); ++failures; }
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
        br_image_save_bmp(&e->framebuffer, "editor_script_error.bmp");
        ed_cmd_play(ed);
        frames(e, 2);
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
        layout l = make_layout(e);
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
    failures += test_assets(e);
    printf("editor (headless): %s - wrote editor*.bmp, editor_demo.scene, editor_project/\n",
           failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}

/* ---- main --------------------------------------------------------------------------- */

static void script_print(const char *text, void *user) {   /* print() in a script: the status bar */
    BR_UNUSED(user);
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
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) ED.headless = true;
        else g_open_path = argv[i];
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
    if (ED.headless) cfg.platform = br_platform_headless();
    if (!br_engine_init(e, &cfg)) { br_free(e); return 1; }

    if (ED.headless) rc = run_headless(e);
    else while (br_engine_frame(e)) { /* the editor scene does the rest */ }

    br_engine_shutdown(e);
    br_free(e);
    return rc;
}
