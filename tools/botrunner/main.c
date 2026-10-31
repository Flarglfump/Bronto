/*
 * botrunner - runs a game headless for a bot in another program, and records the run
 * (replay.h). The bot talks to it over stdin and stdout, a line at a time: it presses
 * keys, lets frames happen, and asks about the game (entities, script variables, tile
 * maps). Nothing it does reaches the game except through the keyboard, so the
 * recording plays back exactly with
 *
 *   bronto_player <game> --replay run.brec
 *
 * The scenes are run as bronto_player runs them (load_scene, push_scene, pop_scene).
 *
 *   botrunner <project folder> [--test-scene file.scene]
 *
 * Commands (each answer ends with a line holding just "."):
 *
 *   key K 1|0                 key K (input.h's numbers) down / up, from the next frame on
 *   frame N                   let N frames happen       -> "frame <frames so far> <script errors> <fixed steps so far>"
 *   steps N                   how many fixed steps (script updates) each of the next N frames will run,
 *                             worked out as the engine does with the replay's clock (0, 1 or 2 a frame)
 *   seg NAME                  a new segment starts here
 *   prefix FILE.brec N        plays an earlier recording's keys and segments (as fast as it goes)
 *                             up to frame N, recording them again -> "frame <n>", then "held K..."
 *                             (the keys down at the end), so a bot can carry on from there
 *   scenes                    the scene stack, bottom first: "<i> <file>"
 *   find NAME                 entities of that Name: "<scene> <id>" (every scene)
 *   tagged TAG                entities with that tag: "<scene> <id> <name> <x> <y>"
 *   var S ID V1 V2 ...        script variables, a line each ("?" if it has none): numbers and
 *                             bools as numbers, vec2s as (x, y), entities as #id, the rest as printed
 *   pos S ID                  world position: "<x> <y>" ("none" if it's gone)
 *   child S ID NAME           a child by name: "<id>" (0 if none)
 *   map S ID                  a tile map: "<columns> <rows> <tile size> <x0> <y0> <solid chars>",
 *                             then a line per row (x0, y0: the top-left corner)
 *   draw                      draw the next frame (to save it)
 *   save FILE.bmp             the last frame drawn
 *   finish FILE.brec          write the recording   -> "<frames>"
 *   quit
 */
#include "bronto/bronto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static br_project g_project;
static bool g_failed;
static char g_pending[BR_PATH_MAX];
static bool g_ended;

/* ---- the game, as bronto_player runs it ---- */
static bool game_enter(br_scene *s) {
    char path[BR_PATH_MAX * 2], err[256];
    snprintf((char *)s->data, BR_PATH_MAX, "%s", g_pending);
    br_builtin_register(&s->world, NULL);
    snprintf(path, sizeof(path), "%s%s", br_assets_root(), g_pending);
    if (br_world_load_file(&s->world, path, NULL, 0, err, sizeof(err)) < 0) {
        fprintf(stderr, "botrunner: can't load %s: %s\n", g_pending, err);
        g_failed = true;
        return false;
    }
    br_camera_snap(&s->world);
    return true;
}
static void game_pause(br_scene *s) { br_script_notify(&s->world, "on_pause"); }
static void game_resume(br_scene *s) { br_script_notify(&s->world, "on_resume"); }
static const br_scene_desc *game_desc(bool transparent, bool update_below);
static void game_update(br_scene *s, float dt) {
    char next[BR_PATH_MAX];
    bool transparent = true, below = false;
    BR_UNUSED(dt);
    if (br_script_quit_request(&s->world)) br_engine_quit(s->engine);
    if (br_script_scene_request(&s->world, next, sizeof(next))) {
        snprintf(g_pending, sizeof(g_pending), "%s", next);
        br_scene_clear(&s->engine->scenes);
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

/* ---- answering ---- */
static br_engine *g_e;

static br_world *scene_world(int i) {
    if (i < 0 || i >= br_scenes_depth(&g_e->scenes)) return NULL;
    return &g_e->scenes.stack[i]->world;
}

static void done(void) { fputs(".\n", stdout); fflush(stdout); }

static int errors_now(void) {
    int i, n = 0;
    for (i = 0; i < br_scenes_depth(&g_e->scenes); ++i) n += (int)br_script_error_count(scene_world(i));
    return n;
}

static void cmd_find(const char *name) {
    int s;
    for (s = 0; s < br_scenes_depth(&g_e->scenes); ++s) {
        br_world *w = scene_world(s);
        br_builtin_ids ids;
        const br_component_pool *pool;
        uint32_t i;
        if (!br_builtin_ids_of(w, &ids)) continue;
        pool = &w->pools[ids.name];
        for (i = 0; i < pool->count; ++i) {
            const br_name *nm = (const br_name *)(const void *)(pool->data + (size_t)i * pool->stride);
            if (strcmp(nm->text, name) == 0) printf("%d %u\n", s, (unsigned)pool->entities[i]);
        }
    }
}

static const char *name_of(br_world *w, br_entity e) {
    br_builtin_ids ids;
    const br_name *nm;
    if (!br_builtin_ids_of(w, &ids)) return "";
    nm = (const br_name *)br_get(w, e, ids.name);
    return nm && nm->text[0] ? nm->text : "-";
}

static void cmd_tagged(const char *tag) {
    static br_entity found[4096];
    int s;
    for (s = 0; s < br_scenes_depth(&g_e->scenes); ++s) {
        br_world *w = scene_world(s);
        const char *tags[1];
        uint32_t n, i;
        tags[0] = tag;
        n = br_find_tagged(w, tags, 1, found, 4096);
        if (n > 4096) n = 4096;
        for (i = 0; i < n; ++i) {
            br_vec2 p = br_world_transform(w, found[i]).position;
            char nm[64];
            int k;
            snprintf(nm, sizeof(nm), "%s", name_of(w, found[i]));
            for (k = 0; nm[k]; ++k) if (nm[k] == ' ') nm[k] = '_';
            printf("%d %u %s %.9g %.9g\n", s, (unsigned)found[i], nm, p.x, p.y);
        }
    }
}

static void cmd_vars(int s, br_entity e, char *rest) {
    br_world *w = scene_world(s);
    char *v = strtok(rest, " \t\r\n");
    static char buf[65536];
    while (v) {
        double d, d2;
        br_entity ent = BR_ENTITY_NULL;
        buf[0] = 0;
        if (w && br_entity_alive(w, e) && br_script_get_number(w, e, v, &d)) printf("%.17g\n", d);
        else if (w && br_entity_alive(w, e) && br_script_get_vec2(w, e, v, &d, &d2)) printf("(%.17g, %.17g)\n", d, d2);
        else if (w && br_entity_alive(w, e) && br_script_get_entity(w, e, v, &ent)) printf("#%u\n", (unsigned)ent);
        else if (w && br_entity_alive(w, e) && br_script_get_string(w, e, v, buf, sizeof(buf))) {
            char *p;
            for (p = buf; *p; ++p) if (*p == '\n') *p = ' ';
            printf("%s\n", buf);
        } else printf("?\n");
        v = strtok(NULL, " \t\r\n");
    }
}

static void cmd_map(int s, br_entity e) {
    br_world *w = scene_world(s);
    br_component tc;
    const br_tilemap *m;
    br_vec2 c0;
    int r;
    if (!w || !br_entity_alive(w, e)) { printf("none\n"); return; }
    tc = br_component_find(w, "Tilemap");
    m = (const br_tilemap *)br_get(w, e, tc);
    if (!m || !br_tilemap_cell_center(w, e, 0, 0, &c0)) { printf("none\n"); return; }
    printf("%d %d %d %.9g %.9g %s\n", m->columns, m->rows, m->tile_size, c0.x - m->tile_size / 2.0, c0.y - m->tile_size / 2.0,
           m->solid[0] ? m->solid : "-");
    for (r = 0; r < m->rows; ++r) {
        int c;
        for (c = 0; c < m->columns; ++c) {
            char ch = br_tilemap_cell(w, e, c, r);
            putchar(ch ? ch : ' ');
        }
        putchar('\n');
    }
}

int main(int argc, char **argv) {
    br_engine_config cfg = br_engine_default_config();
    char save_name[160], line[4096];
    const char *game = NULL, *scene = NULL;
    int i;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--test-scene") == 0 && i + 1 < argc) scene = argv[++i];
        else game = argv[i];
    }
    if (!game || !br_fs_is_dir(game)) { fprintf(stderr, "usage: botrunner <project folder> [--test-scene file.scene]\n"); return 1; }
    br_log_set_level(BR_LOG_WARN);
    br_project_defaults(&g_project, "Bronto game");
    br_assets_set_root(game);
    br_project_load(game, &g_project);
    snprintf(g_pending, sizeof(g_pending), "%s", scene ? scene : g_project.main_scene);
    br_script_set_strict(g_project.strict_typing);

    /* as a replay runs it: saved data of its own, starting empty */
    snprintf(save_name, sizeof(save_name), "%s (replay)", g_project.name);
    br_save_set_game(save_name);
    br_replay_clear_saves();
    cfg.title = g_project.name;
    cfg.save_name = save_name;
    cfg.width = g_project.width;
    cfg.height = g_project.height;
    cfg.window_scale = g_project.window_scale;
    cfg.initial_scene = game_desc(false, false);
    cfg.touch_controls = NULL;
    cfg.pause_when_minimized = false;
    cfg.audio_sample_rate = 0;
    br_replay_record(g_project.name, 30);
    cfg.platform = br_replay_platform(br_platform_headless(), false);
    g_e = BR_NEW(br_engine);
    if (!g_e || !br_engine_init(g_e, &cfg) || g_failed) { fprintf(stderr, "botrunner: the game didn't start\n"); return 1; }
    br_replay_attach(g_e);
    printf("ready %s\n", g_project.name);
    done();

    while (fgets(line, sizeof(line), stdin)) {
        char cmd[32] = "", rest[4000] = "";
        int s = 0;
        unsigned id = 0;
        sscanf(line, "%31s %3999[^\n]", cmd, rest);
        if (strcmp(cmd, "key") == 0) {
            int k = 0, d = 0;
            sscanf(rest, "%d %d", &k, &d);
            if (k > 0 && k < BR_KEY_COUNT) br_replay_key((br_key)k, d != 0);
        } else if (strcmp(cmd, "mouse") == 0) {
            /* mouse move <dx> <dy> | mouse at <x> <y> (window pixels) | mouse button <0-2> <down>:
             * straight to the engine (not recorded), for trying mouse controls */
            static float mx = 100, my = 100;
            char what[16] = "";
            float p1 = 0, p2 = 0;
            br_event ev;
            sscanf(rest, "%15s %f %f", what, &p1, &p2);
            if (strcmp(what, "move") == 0) {
                ev = br_event_mouse_move(mx, my, p1, p2);
                br_events_post(&g_e->events, &ev);
            } else if (strcmp(what, "at") == 0) {
                ev = br_event_mouse_move(p1, p2, p1 - mx, p2 - my);
                mx = p1; my = p2;
                br_events_post(&g_e->events, &ev);
            } else if (strcmp(what, "button") == 0) {
                ev = br_event_make(p2 != 0 ? BR_EVENT_MOUSE_BUTTON_DOWN : BR_EVENT_MOUSE_BUTTON_UP);
                ev.u.mouse_button.button = (br_mouse_button)(int)p1;
                ev.u.mouse_button.x = ev.u.mouse_button.wx = mx;
                ev.u.mouse_button.y = ev.u.mouse_button.wy = my;
                ev.u.mouse_button.clicks = 1;
                br_events_post(&g_e->events, &ev);
            }
            printf("locked %d\n", br_headless_mouse_locked() ? 1 : 0);
        } else if (strcmp(cmd, "frame") == 0) {
            long n = atol(rest);
            while (n-- > 0 && !g_ended) {
                if (!br_engine_frame(g_e)) g_ended = true;
            }
            printf("frame %llu %d %llu%s\n", (unsigned long long)br_replay_frame(), errors_now(),
                   (unsigned long long)g_e->fixed_steps, g_ended ? " ended" : "");
            if (errors_now()) {
                for (i = 0; i < br_scenes_depth(&g_e->scenes); ++i)
                    if (br_script_error_count(scene_world(i))) printf("error %s\n", br_script_last_error(scene_world(i)));
            }
        } else if (strcmp(cmd, "steps") == 0) {
            long n = atol(rest);
            double now = g_e->last_clock, acc = g_e->accumulator, fts = g_e->config.fixed_timestep;
            while (n-- > 0) {
                double next = now + 1.0 / 60.0, dt = next - now;
                int k = 0;
                now = next;
                if (dt < 0) dt = 0;
                if (dt > g_e->config.max_frame_time) dt = g_e->config.max_frame_time;
                acc += dt;
                while (acc >= fts && k < g_e->config.max_fixed_steps) { acc -= fts; k++; }
                if (k == g_e->config.max_fixed_steps && acc >= fts) acc = 0;
                printf("%d", k);
            }
            printf("\n");
        } else if (strcmp(cmd, "prefix") == 0) {
            char path[3000] = "";
            unsigned long long upto = 0;
            static bool down[BR_KEY_COUNT];
            FILE *f;
            sscanf(rest, "%2999s %llu", path, &upto);
            f = fopen(path, "r");
            if (!f) printf("error can't read %s\n", path);
            else {
                char l[512];
                int k;
                bool more = fgets(l, sizeof(l), f) != NULL;
                while (!g_ended && br_replay_frame() < upto) {
                    unsigned long long now = br_replay_frame(), at;
                    while (more) {                      /* everything recorded for this frame */
                        char kind[8] = "";
                        if (sscanf(l, "%7s %llu", kind, &at) == 2 && (kind[0] == 'k' || kind[0] == 's') && kind[1] == 0) {
                            if (at > now) break;
                            if (at == now && kind[0] == 'k') {
                                char sgn = 0;
                                int key = 0;
                                if (sscanf(l, "k %llu %c%d", &at, &sgn, &key) == 3 && key > 0 && key < BR_KEY_COUNT) {
                                    down[key] = sgn == '+';
                                    br_replay_key((br_key)key, sgn == '+');
                                }
                            } else if (at == now && kind[0] == 's') {
                                char name[256] = "";
                                if (sscanf(l, "seg %llu %255[^\n]", &at, name) >= 2) br_replay_segment(name);
                            }
                        }
                        more = fgets(l, sizeof(l), f) != NULL;
                    }
                    if (!br_engine_frame(g_e)) g_ended = true;
                }
                fclose(f);
                printf("frame %llu %d %llu%s\n", (unsigned long long)br_replay_frame(), errors_now(),
                       (unsigned long long)g_e->fixed_steps, g_ended ? " ended" : "");
                printf("held");
                for (k = 0; k < BR_KEY_COUNT; ++k) if (down[k]) printf(" %d", k);
                printf("\n");
            }
        } else if (strcmp(cmd, "seg") == 0) {
            br_replay_segment(rest);
        } else if (strcmp(cmd, "scenes") == 0) {
            for (i = 0; i < br_scenes_depth(&g_e->scenes); ++i) printf("%d %s\n", i, (const char *)g_e->scenes.stack[i]->data);
        } else if (strcmp(cmd, "find") == 0) {
            cmd_find(rest);
        } else if (strcmp(cmd, "tagged") == 0) {
            cmd_tagged(rest);
        } else if (strcmp(cmd, "var") == 0) {
            int used = 0;
            if (sscanf(rest, "%d %u %n", &s, &id, &used) >= 2) cmd_vars(s, (br_entity)id, rest + used);
        } else if (strcmp(cmd, "pos") == 0) {
            br_world *w;
            sscanf(rest, "%d %u", &s, &id);
            w = scene_world(s);
            if (w && br_entity_alive(w, (br_entity)id)) {
                br_vec2 p = br_world_transform(w, (br_entity)id).position;
                printf("%.9g %.9g\n", p.x, p.y);
            } else printf("none\n");
        } else if (strcmp(cmd, "child") == 0) {
            char nm[256] = "";
            br_world *w;
            sscanf(rest, "%d %u %255[^\n]", &s, &id, nm);
            w = scene_world(s);
            printf("%u\n", w && br_entity_alive(w, (br_entity)id) ? (unsigned)br_child_named(w, (br_entity)id, nm) : 0u);
        } else if (strcmp(cmd, "map") == 0) {
            sscanf(rest, "%d %u", &s, &id);
            cmd_map(s, (br_entity)id);
        } else if (strcmp(cmd, "draw") == 0) {
            br_replay_draw_every_frame(true);
            if (!br_engine_frame(g_e)) g_ended = true;
            br_replay_draw_every_frame(false);
            printf("frame %llu\n", (unsigned long long)br_replay_frame());
        } else if (strcmp(cmd, "save") == 0) {
            printf("%s\n", br_image_save_bmp(&g_e->framebuffer, rest) ? "ok" : "failed");
        } else if (strcmp(cmd, "finish") == 0) {
            uint64_t f = br_replay_frame();
            printf("%llu %s\n", (unsigned long long)f, br_replay_finish(rest) ? "ok" : "failed");
        } else if (strcmp(cmd, "quit") == 0) {
            done();
            break;
        } else {
            printf("unknown %s\n", cmd);
        }
        done();
    }
    br_engine_shutdown(g_e);
    br_free(g_e);
    return 0;
}
