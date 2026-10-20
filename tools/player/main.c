/*
 * BRONTO PLAYER - runs a game made with the Bronto editor.
 *
 *   bronto_player                   the game packed into this program (what the editor's
 *                                   Build makes), or a game.brpak beside it
 *   bronto_player <project folder>  a project folder as it is: its project.bronto settings
 *                                   and its main scene
 *   bronto_player <game.brpak>      a pack file
 *   bronto_player <file.scene>      one scene (its project folder is found as the editor does)
 *
 *   --headless          no window or sound: run --frames N steps (default 120) and exit;
 *                       the exit status is 1 if the game didn't load or a script failed
 *   --frames N          (with --headless)
 *   --screenshot f.bmp  (with --headless) save the last frame
 *
 * The game is the project's scenes, scripts and assets: the engine's
 * built-in components and systems do the rest, exactly as in the editor's
 * Play. Scripts can call load_scene(path) to move to another scene and
 * quit() to end the game.
 */
#include "bronto/bronto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static br_project g_project;
static char g_root[BR_PATH_MAX];               /* the project folder ("" in a pack) */
static char g_scene[BR_PATH_MAX];              /* the scene to show, relative to the root */
static bool g_failed;

static bool game_enter(br_scene *s) {
    char path[BR_PATH_MAX * 2], err[256];
    int n;
    br_builtin_register(&s->world, NULL);
    snprintf(path, sizeof(path), "%s%s", br_assets_root(), g_scene);
    n = br_world_load_file(&s->world, path, NULL, 0, err, sizeof(err));
    if (n < 0) {
        BR_ERROR("can't load the scene %s: %s", g_scene, err);
        g_failed = true;
        return false;
    }
    BR_INFO("playing %s (%d entities)", g_scene, n);
    return true;
}

static const br_scene_desc *game_desc(void);

/* After each step: what the scripts asked for. */
static void game_update(br_scene *s, float dt) {
    char next[BR_PATH_MAX];
    BR_UNUSED(dt);
    if (br_script_quit_request(&s->world)) br_engine_quit(s->engine);
    if (br_script_scene_request(&s->world, next, sizeof(next))) {
        snprintf(g_scene, sizeof(g_scene), "%s", next);
        br_scene_switch(&s->engine->scenes, game_desc());   /* a fresh world, at a safe point */
    }
}

static const br_scene_desc k_game = {
    "game", 0, false, false, game_enter, NULL, NULL, NULL, NULL, game_update, NULL, NULL
};
static const br_scene_desc *game_desc(void) { return &k_game; }

static bool on_init(br_engine *e) {
    if (g_project.icon[0]) {                             /* the project's icon, for the window and taskbar */
        const br_image *icon = br_asset_image(g_project.icon);
        if (icon) br_window_set_icon(e, icon);
    }
    return true;
}

static bool has_ext(const char *p, const char *ext) { return br_path_has_ext(p, ext); }

/* Finds the game: an argument, the pack in this program, or one beside it. */
static bool find_game(const char *arg) {
    char exe[BR_PATH_MAX], dir[BR_PATH_MAX], candidate[BR_PATH_MAX + 64], stem[128];
    if (arg && br_fs_is_dir(arg)) {                      /* a project folder */
        snprintf(g_root, sizeof(g_root), "%s", arg);
        br_assets_set_root(arg);
        br_project_load(arg, &g_project);
        snprintf(g_scene, sizeof(g_scene), "%s", g_project.main_scene);
        return true;
    }
    if (arg && has_ext(arg, ".scene")) {                 /* one scene */
        br_assets_find_root(arg, g_root, sizeof(g_root));
        br_assets_set_root(g_root);
        br_project_load(g_root, &g_project);
        snprintf(g_scene, sizeof(g_scene), "%s", g_root[0] && strncmp(arg, g_root, strlen(g_root)) == 0 ? arg + strlen(g_root) + 1 : arg);
        return true;
    }
    if (arg) {                                           /* a pack */
        if (!br_pack_mount(arg)) { fprintf(stderr, "bronto_player: %s isn't a project folder, a scene or a pack\n", arg); return false; }
    } else if (!br_fs_executable_path(exe, sizeof(exe)) || !br_pack_mount(exe)) {
        bool found = false;                              /* not packed in: game.brpak beside it */
        br_path_parent(dir, sizeof(dir), br_fs_executable_path(exe, sizeof(exe)) ? exe : "");
        br_path_stem(stem, sizeof(stem), exe);
        br_path_join(candidate, sizeof(candidate), dir, "game.brpak");
        found = br_pack_mount(candidate);
        if (!found) { snprintf(candidate, sizeof(candidate), "%s/../Resources/game.brpak", dir); found = br_pack_mount(candidate); }   /* a Mac .app */
        if (!found) { char name[160]; snprintf(name, sizeof(name), "%s.brpak", stem); br_path_join(candidate, sizeof(candidate), dir, name); found = br_pack_mount(candidate); }
        if (!found) return false;
    }
    g_root[0] = 0;                                       /* everything comes from the pack */
    br_assets_set_root("");
    br_project_load("", &g_project);
    snprintf(g_scene, sizeof(g_scene), "%s", g_project.main_scene);
    return true;
}

int main(int argc, char **argv) {
    br_engine *e;
    br_engine_config cfg = br_engine_default_config();
    const char *arg = NULL, *shot = NULL;
    bool headless = false;
    long frames = 120;
    int i, rc = 0;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) headless = true;
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = atol(argv[++i]);
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) shot = argv[++i];
        else if (argv[i][0] == '-' && argv[i][1] == 'p' && strncmp(argv[i], "-psn_", 5) == 0) { /* macOS Finder's process id: ignore */ }
        else arg = argv[i];
    }
    br_project_defaults(&g_project, "Bronto game");
    if (!find_game(arg)) {
        fprintf(stderr,
                "bronto_player: no game to play.\n"
                "  bronto_player <project folder> | <file.scene> | <game.brpak> [--headless [--frames N] [--screenshot f.bmp]]\n"
                "Games built by the editor (Build menu) have their project packed in and need no arguments.\n");
        return 1;
    }
    br_script_set_strict(g_project.strict_typing);
    cfg.title = g_project.name;
    cfg.width = g_project.width;
    cfg.height = g_project.height;
    cfg.window_scale = g_project.window_scale;
    cfg.initial_scene = &k_game;
    cfg.on_init = on_init;
    if (headless) { cfg.platform = br_platform_headless(); cfg.audio_sample_rate = 0; }
    e = BR_NEW(br_engine);
    if (!e || !br_engine_init(e, &cfg)) { br_free(e); br_pack_unmount(); return 1; }
    if (br_scenes_depth(&e->scenes) == 0 || g_failed) {
        rc = 1;
    } else if (headless) {
        long k;
        for (k = 0; k < frames && br_engine_frame(e); ++k) {}
        {
            br_scene *top = br_scenes_top(&e->scenes);
            uint32_t errors = top ? br_script_error_count(&top->world) : 0;
            if (shot) br_image_save_bmp(&e->framebuffer, shot);
            if (g_failed || !top || errors) rc = 1;
            printf("%s (headless): %ld frames of %s, %u script error%s%s\n", g_project.name, k, g_scene, errors,
                   errors == 1 ? "" : "s", shot ? ", saved a screenshot" : "");
            if (errors && top) printf("  last: %s\n", br_script_last_error(&top->world));
        }
    } else {
        br_engine_loop(e, NULL);                         /* the scene's systems do everything (on the web, */
        if (g_failed) rc = 1;                             /* the browser runs the frames; this isn't reached) */
    }
    br_engine_shutdown(e);
    br_free(e);
    br_pack_unmount();
    return rc;
}
