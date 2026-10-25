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
 *   --replay run.brec   play a recorded run back, frame for frame (replay.h): in a
 *                       window with playback keys, or with --headless as fast as it
 *                       goes, checking every recorded frame (exit status 1 if one differs)
 *   --segment N         (with --replay) start at segment N (1 = the start)
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
static char g_main_scene[BR_PATH_MAX];         /* the first scene (a replay going back starts here again) */

/* Each scene on the stack is a project scene file: its path is the scene's data.
 * load_scene() replaces the whole stack; push_scene() puts one on top (transparent:
 * the ones under it still show; update_below: they keep running), pop_scene()
 * takes the top one off. */
static char g_pending[BR_PATH_MAX];            /* the scene file the next scene to enter loads */

static bool game_enter(br_scene *s) {
    char path[BR_PATH_MAX * 2], err[256];
    int n;
    snprintf((char *)s->data, BR_PATH_MAX, "%s", g_pending);
    br_builtin_register(&s->world, NULL);
    snprintf(path, sizeof(path), "%s%s", br_assets_root(), g_pending);
    n = br_world_load_file(&s->world, path, NULL, 0, err, sizeof(err));
    if (n < 0) {
        BR_ERROR("can't load the scene %s: %s", g_pending, err);
        g_failed = true;
        return false;
    }
    br_camera_snap(&s->world);                           /* cameras start where they look */
    BR_INFO("playing %s (%d entities)", g_pending, n);
    return true;
}

static void game_pause(br_scene *s) { br_script_notify(&s->world, "on_pause"); }
static void game_resume(br_scene *s) { br_script_notify(&s->world, "on_resume"); }

static const br_scene_desc *game_desc(bool transparent, bool update_below);

/* After each step: what the scripts asked for. */
static void game_update(br_scene *s, float dt) {
    char next[BR_PATH_MAX];
    bool transparent = true, below = false;
    BR_UNUSED(dt);
    if (br_script_quit_request(&s->world)) br_engine_quit(s->engine);
    if (br_script_scene_request(&s->world, next, sizeof(next))) {
        snprintf(g_pending, sizeof(g_pending), "%s", next);
        snprintf(g_scene, sizeof(g_scene), "%s", next);
        br_scene_clear(&s->engine->scenes);              /* a fresh stack, at a safe point */
        br_scene_push(&s->engine->scenes, game_desc(false, false));
        return;
    }
    if (br_script_pop_request(&s->world) && br_scenes_depth(&s->engine->scenes) > 1) br_scene_pop(&s->engine->scenes);
    if (br_script_push_request(&s->world, next, sizeof(next), &transparent, &below)) {
        snprintf(g_pending, sizeof(g_pending), "%s", next);
        br_scene_push(&s->engine->scenes, game_desc(transparent, below));
    }
}

static const br_scene_desc k_game[4] = {
    { "game", BR_PATH_MAX, false, false, game_enter, NULL, game_pause, game_resume, NULL, game_update, NULL, NULL },
    { "game (transparent)", BR_PATH_MAX, true, false, game_enter, NULL, game_pause, game_resume, NULL, game_update, NULL, NULL },
    { "game (updates below)", BR_PATH_MAX, false, true, game_enter, NULL, game_pause, game_resume, NULL, game_update, NULL, NULL },
    { "game (transparent, updates below)", BR_PATH_MAX, true, true, game_enter, NULL, game_pause, game_resume, NULL, game_update, NULL, NULL },
};
static const br_scene_desc *game_desc(bool transparent, bool update_below) {
    return &k_game[(transparent ? 1 : 0) + (update_below ? 2 : 0)];
}

static bool on_init(br_engine *e) {
    if (g_project.icon[0]) {                             /* the project's icon, for the window and taskbar */
        const br_image *icon = br_asset_image(g_project.icon);
        if (icon) br_window_set_icon(e, icon);
    }
    return true;
}

/* A replay going back to an earlier segment starts the game again. */
static void replay_restart(br_engine *e) {
    BR_UNUSED(e);
    snprintf(g_scene, sizeof(g_scene), "%s", g_main_scene);
    snprintf(g_pending, sizeof(g_pending), "%s", g_main_scene);
    br_script_reset_session();                           /* the globals and music of the run before */
    g_failed = false;
}

static void replay_after_frame(br_engine *e) {
    if (!br_replay_after_frame(e)) br_engine_quit(e);
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
    const char *arg = NULL, *shot = NULL, *replay = NULL;
    char save_name[160];
    int segment = 1;
    bool headless = false, frames_given = false, touch = false;
    long frames = 120;
    int i, rc = 0;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) headless = true;
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) { frames = atol(argv[++i]); frames_given = true; }
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) shot = argv[++i];
        else if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) replay = argv[++i];
        else if (strcmp(argv[i], "--segment") == 0 && i + 1 < argc) segment = atoi(argv[++i]);
        else if (strcmp(argv[i], "--touch") == 0) touch = true;
        else if (argv[i][0] == '-' && argv[i][1] == 'p' && strncmp(argv[i], "-psn_", 5) == 0) { /* macOS Finder's process id: ignore */ }
        else arg = argv[i];
    }
    br_project_defaults(&g_project, "Bronto game");
    if (!find_game(arg)) {
        fprintf(stderr,
                "bronto_player: no game to play.\n"
                "  bronto_player <project folder> | <file.scene> | <game.brpak> [--touch] [--headless [--frames N] [--screenshot f.bmp]]\n"
                "  bronto_player <game> --replay run.brec [--segment N] [--headless]\n"
                "Games built by the editor (Build menu) have their project packed in and need no arguments.\n");
        return 1;
    }
    snprintf(g_main_scene, sizeof(g_main_scene), "%s", g_scene);
    if (replay && !br_replay_load(replay)) { br_pack_unmount(); return 1; }
    br_script_set_strict(g_project.strict_typing);
    cfg.title = g_project.name;
    cfg.width = g_project.width;
    cfg.height = g_project.height;
    cfg.window_scale = g_project.window_scale;
    cfg.initial_scene = game_desc(false, false);
    snprintf(g_pending, sizeof(g_pending), "%s", g_scene);
    cfg.on_init = on_init;
    cfg.touch_controls = replay ? NULL : g_project.touch_controls;   /* (a replay is keys only) */
    cfg.touch_mouse = touch && !replay;                               /* --touch: try them with the mouse */
    if (headless) { cfg.platform = br_platform_headless(); cfg.audio_sample_rate = 0; }
    if (replay) {
        /* Its own saved data, starting empty, as the recorded run's did. */
        snprintf(save_name, sizeof(save_name), "%s (replay)", g_project.name);
        cfg.save_name = save_name;
        br_save_set_game(save_name);
        br_replay_clear_saves();
        cfg.pause_when_minimized = false;
        cfg.platform = br_replay_platform(headless ? br_platform_headless() : br_platform_native(), !headless);
        br_replay_set_restart_hook(replay_restart);
        if (segment > 1) br_replay_start_at_segment(segment - 1);
    }
    e = BR_NEW(br_engine);
    if (!e || !br_engine_init(e, &cfg)) { br_free(e); br_pack_unmount(); return 1; }
    if (replay) br_replay_attach(e);
    if (br_scenes_depth(&e->scenes) == 0 || g_failed) {
        rc = 1;
    } else if (replay && headless) {
        uint32_t checked, bad;
        uint64_t first;
        while (br_engine_frame(e) && br_replay_after_frame(e)) {
            if (frames_given && (long)br_replay_frame() >= frames) break;      /* (--frames: stop there) */
            if (frames_given && (long)br_replay_frame() >= frames - 1) br_replay_draw_every_frame(true);
        }
        bad = br_replay_mismatches(&checked, &first);
        {
            br_scene *top = br_scenes_top(&e->scenes);
            uint32_t errors = top ? br_script_error_count(&top->world) : 0;
            printf("%s (replay): %llu frames, %u of %u checked frames differed, %u script error%s\n", g_project.name,
                   (unsigned long long)br_replay_frame(), bad, checked, errors, errors == 1 ? "" : "s");
            if (bad) printf("  first difference: frame %llu\n", (unsigned long long)first);
            if (shot) br_image_save_bmp(&e->framebuffer, shot);
            if (bad || errors || (!br_replay_ended() && !frames_given)) rc = 1;
        }
    } else if (replay) {
        br_engine_loop(e, replay_after_frame);
        if (g_failed) rc = 1;
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
