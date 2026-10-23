/*
 * pantrybot - plays Pantry Quest through and records it (see bot.h).
 *
 *   pantrybot [--out run.brec] [--game examples/pantryquest] [--shots dir] [--quiet]
 *
 * The run is headless and as fast as it goes. The recording plays back with
 *   bronto_player examples/pantryquest --replay run.brec
 */
#include "bot.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static br_project g_project;
static char g_scene[BR_PATH_MAX];
static bool g_failed;
static br_engine *g_engine;

/* ---- the game, as bronto_player runs it ---- */
static bool game_enter(br_scene *s) {
    char path[BR_PATH_MAX * 2], err[256];
    br_builtin_register(&s->world, NULL);
    snprintf(path, sizeof(path), "%s%s", br_assets_root(), g_scene);
    if (br_world_load_file(&s->world, path, NULL, 0, err, sizeof(err)) < 0) {
        fprintf(stderr, "pantrybot: can't load %s: %s\n", g_scene, err);
        g_failed = true;
        return false;
    }
    return true;
}
static const br_scene_desc *game_desc(void);
static void game_update(br_scene *s, float dt) {
    char next[BR_PATH_MAX];
    BR_UNUSED(dt);
    if (br_script_quit_request(&s->world)) br_engine_quit(s->engine);
    if (br_script_scene_request(&s->world, next, sizeof(next))) {
        snprintf(g_scene, sizeof(g_scene), "%s", next);
        br_scene_switch(&s->engine->scenes, game_desc());
    }
}
static const br_scene_desc k_game = {
    "game", 0, false, false, game_enter, NULL, NULL, NULL, NULL, game_update, NULL, NULL
};
static const br_scene_desc *game_desc(void) { return &k_game; }

/* ---- the bot's thread, one frame at a time ---- */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static int g_turn;            /* 0: the bot's turn, 1: the engine's */
static bool g_bot_done, g_stop;
static bool g_quiet;
static const char *g_test_vars;          /* --test: start in main.scene with these Game vars */
const char *g_test_route;               /* --route: which part of the route to run */

void bot_frame(void) {
    pthread_mutex_lock(&g_mu);
    g_turn = 1;
    pthread_cond_broadcast(&g_cv);
    while (g_turn == 1) pthread_cond_wait(&g_cv, &g_mu);
    pthread_mutex_unlock(&g_mu);
    if (g_stop) pthread_exit(NULL);
}

void bot_wait(int frames) { while (frames-- > 0) bot_frame(); }

br_world *bot_world(void) {
    br_scene *top = br_scenes_top(&g_engine->scenes);
    return top ? &top->world : NULL;
}
br_engine *bot_engine(void) { return g_engine; }
uint64_t bot_frames(void) { return br_replay_frame(); }

void bot_log(const char *fmt, ...) {
    va_list ap;
    uint64_t f = bot_frames();
    unsigned s = (unsigned)(f / 60);
    if (g_quiet) return;
    printf("[%6llu %u:%02u:%02u] ", (unsigned long long)f, s / 3600, (s / 60) % 60, s % 60);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

void bot_fail(const char *fmt, ...) {
    va_list ap;
    printf("FAILED at frame %llu: ", (unsigned long long)bot_frames());
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    g_failed = true;
    pthread_mutex_lock(&g_mu);
    g_bot_done = true;
    g_turn = 1;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    pthread_exit(NULL);
}

static void *bot_main(void *arg) {
    BR_UNUSED(arg);
    pthread_mutex_lock(&g_mu);
    while (g_turn == 1) pthread_cond_wait(&g_cv, &g_mu);
    pthread_mutex_unlock(&g_mu);
    bot_route();
    pthread_mutex_lock(&g_mu);
    g_bot_done = true;
    g_turn = 1;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    return NULL;
}

/* ---- keys ---- */
static bool g_held[BR_KEY_COUNT];

void key_hold(br_key k, bool down) {
    if (g_held[k] == down) return;
    g_held[k] = down;
    br_replay_key(k, down);
}
void keys_release_all(void) {
    int k;
    for (k = 0; k < BR_KEY_COUNT; ++k) if (g_held[k]) key_hold((br_key)k, false);
}
void key_tap(br_key k) {
    key_hold(k, true);
    bot_frame();
    key_hold(k, false);
    bot_frame();
}
void key_hold_for(br_key k, int frames) {
    key_hold(k, true);
    bot_wait(frames);
    key_hold(k, false);
}

int main(int argc, char **argv) {
    const char *game = "examples/pantryquest", *out = "pantryquest.brec";
    br_engine_config cfg = br_engine_default_config();
    char save_name[160];
    pthread_t th;
    int i, rc = 0;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else if (strcmp(argv[i], "--game") == 0 && i + 1 < argc) game = argv[++i];
        else if (strcmp(argv[i], "--quiet") == 0) g_quiet = true;
        else if (strcmp(argv[i], "--test") == 0 && i + 1 < argc) g_test_vars = argv[++i];
        else if (strcmp(argv[i], "--route") == 0 && i + 1 < argc) g_test_route = argv[++i];
        else { fprintf(stderr, "usage: pantrybot [--out run.brec] [--game folder] [--quiet]\n"); return 1; }
    }
    br_log_set_level(BR_LOG_WARN);
    br_project_defaults(&g_project, "Pantry Quest");
    br_assets_set_root(game);
    br_project_load(game, &g_project);
    snprintf(g_scene, sizeof(g_scene), "%s", g_test_vars ? "main.scene" : g_project.main_scene);
    br_script_set_strict(g_project.strict_typing);

    /* the same saved data as a replay: its own folder, empty */
    snprintf(save_name, sizeof(save_name), "%s (replay)", g_project.name);
    br_save_set_game(save_name);
    br_replay_clear_saves();

    cfg.title = g_project.name;
    cfg.save_name = save_name;
    cfg.width = g_project.width;
    cfg.height = g_project.height;
    cfg.window_scale = g_project.window_scale;
    cfg.initial_scene = &k_game;
    cfg.pause_when_minimized = false;
    cfg.audio_sample_rate = 0;
    world_load(game);
    br_replay_record(g_project.name, 30);
    cfg.platform = br_replay_platform(br_platform_headless(), false);
    g_engine = BR_NEW(br_engine);
    if (!g_engine || !br_engine_init(g_engine, &cfg)) return 1;
    br_replay_attach(g_engine);
    if (g_test_vars) {                                   /* (a test: the Game starts where it's told) */
        br_world *w = bot_world();
        br_component sc = br_component_find(w, "Script");
        br_entity game_e = ent_named("Game");
        br_script *s = game_e ? (br_script *)br_get(w, game_e, sc) : NULL;
        if (s) snprintf(s->vars, sizeof(s->vars), "%s", g_test_vars);
    }

    g_turn = 0;
    pthread_create(&th, NULL, bot_main, NULL);
    for (;;) {
        pthread_mutex_lock(&g_mu);
        while (g_turn == 0) pthread_cond_wait(&g_cv, &g_mu);
        pthread_mutex_unlock(&g_mu);
        if (g_bot_done) break;
        if (!br_engine_frame(g_engine)) {
            printf("the game ended at frame %llu\n", (unsigned long long)bot_frames());
            g_stop = true;
            pthread_mutex_lock(&g_mu);
            g_turn = 0;
            pthread_cond_broadcast(&g_cv);
            pthread_mutex_unlock(&g_mu);
            break;
        }
        {
            br_world *w = bot_world();
            if (w && br_script_error_count(w)) {
                printf("script error: %s\n", br_script_last_error(w));
                g_failed = true;
            }
        }
        pthread_mutex_lock(&g_mu);
        g_turn = 0;
        pthread_cond_broadcast(&g_cv);
        pthread_mutex_unlock(&g_mu);
    }
    pthread_join(th, NULL);
    {
        uint64_t frames = br_replay_frame();
        if (!br_replay_finish(out)) rc = 1;
        printf("%s: %llu frames (%u:%02u:%02u)%s\n", out, (unsigned long long)frames, (unsigned)(frames / 216000),
               (unsigned)(frames / 3600 % 60), (unsigned)(frames / 60 % 60), g_failed ? " - the run did not finish" : "");
    }
    br_engine_shutdown(g_engine);
    br_free(g_engine);
    return g_failed ? 1 : rc;
}

/* A picture of the frame just drawn (for looking at what the bot did). */
void bot_shot(const char *name) {
    char path[256];
    br_replay_draw_every_frame(true);                    /* (the next frame is drawn) */
    bot_frame();
    br_replay_draw_every_frame(false);
    snprintf(path, sizeof(path), "/tmp/claude-0/shots/%06llu_%s.bmp", (unsigned long long)bot_frames(), name);
    br_image_save_bmp(&g_engine->framebuffer, path);
}
