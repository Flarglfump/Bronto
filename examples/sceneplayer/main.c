/*
 * SCENE PLAYER - plays a scene made in the Bronto editor.
 *
 *   sceneplayer level.scene     play a scene file
 *   sceneplayer                 play the small scene built into this file
 *   sceneplayer --headless      no window: play 2 seconds, save a screenshot
 *
 * This is all a game needs to use editor scenes: register the built-in
 * components (and any of its own) on a scene's world, load the file, and
 * the engine's systems move, collide, run the scripts and draw
 * everything. Esc quits. Try: sceneplayer examples/platformer/main.scene
 */
#include "bronto/bronto.h"
#include <stdio.h>
#include <string.h>

/* What the editor would save; any scene file works the same way. */
static const char k_builtin_scene[] =
    "bronto_scene 1\n"
    "entity\n  Name\n    text = \"Backdrop\"\n  Transform\n    position = 160 90\n"
    "  Shape\n    size = 320 180\n    color = #182238ff\n    layer = -10\n"
    "entity\n  Name\n    text = \"Title\"\n  Transform\n    position = 160 24\n"
    "  Label\n    text = \"Scene player\"\n    size = 2\n    color = #ffdc5aff\n"
    "entity\n  Name\n    text = \"Ball\"\n  Transform\n    position = 80 90\n"
    "  Shape\n    kind = ellipse\n    size = 18 18\n    color = #ff8c32ff\n"
    "  Motion\n    velocity = 80 -60\n  Bounds\n    min = 9 40\n    max = 311 171\n"
    "entity\n  Name\n    text = \"Spinner\"\n  Transform\n    position = 240 110\n"
    "  Shape\n    size = 24 24\n    color = #aa5ae6ff\n    outline = #ffffffff\n"
    "  Motion\n    velocity = 0 0\n    spin = 120\n"
    "entity\n  Name\n    text = \"Note\"\n  Transform\n    position = 160 166\n"
    "  Label\n    text = \"Make your own in the editor\"\n    size = 1\n    color = #b4bed2ff\n";

static const char *g_path;
static bool g_headless;
static char g_title[128];

static bool player_enter(br_scene *s) {
    char err[200];
    int n;
    br_builtin_register(&s->world, NULL);
    if (g_path) {
        char dir[512];
        br_assets_find_root(g_path, dir, sizeof(dir));
        br_assets_set_root(dir);                      /* the scene's project folder */
        n = br_world_load_file(&s->world, g_path, NULL, 0, err, sizeof(err));
    } else {
        n = br_world_from_text(&s->world, k_builtin_scene, NULL, 0, err, sizeof(err));
    }
    if (n < 0) {
        BR_ERROR("can't load the scene: %s", err);
        return false;
    }
    snprintf(g_title, sizeof(g_title), "%s (%d entities)", g_path ? g_path : "built-in scene", n);
    BR_INFO("playing %s", g_title);
    return true;
}

static void player_update(br_scene *s, float dt) {
    BR_UNUSED(dt);
    if (br_key_pressed(&s->engine->input, BR_KEY_ESCAPE)) br_engine_quit(s->engine);
}

static const br_scene_desc k_player = {
    "player", 0, false, false, player_enter, NULL, NULL, NULL, NULL, player_update, NULL, NULL
};

/* Find something with Motion (or a dynamic RigidBody) and report where it is. */
static br_vec2 first_moving_position(br_engine *e) {
    br_scene *s = br_scenes_top(&e->scenes);
    br_builtin_ids ids;
    br_component with[2];
    br_query q;
    if (!s || !br_builtin_ids_of(&s->world, &ids)) return br_v2(0, 0);
    with[0] = br_component_find(&s->world, "RigidBody");
    with[1] = ids.transform;
    q = br_query_make(&s->world, with, 2);
    while (br_query_next(&q))
        if (BR_QUERY_GET(&q, 0, br_rigidbody)->type == BR_BODY_DYNAMIC) return BR_QUERY_GET(&q, 1, br_transform)->position;
    with[0] = ids.motion;
    with[1] = ids.transform;
    q = br_query_make(&s->world, with, 2);
    while (br_query_next(&q)) {
        const br_motion *m = BR_QUERY_GET(&q, 0, br_motion);
        if (m->velocity.x != 0 || m->velocity.y != 0) return BR_QUERY_GET(&q, 1, br_transform)->position;
    }
    return br_v2(0, 0);
}

int main(int argc, char **argv) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config cfg = br_engine_default_config();
    int i, rc = 0;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) g_headless = true;
        else g_path = argv[i];
    }
    cfg.title = "Bronto Scene Player";
    cfg.width = 320;
    cfg.height = 180;
    cfg.window_scale = 3;
    cfg.scale_mode = BR_SCALE_INTEGER;
    cfg.initial_scene = &k_player;
    if (g_headless) cfg.platform = br_platform_headless();
    if (!br_engine_init(e, &cfg)) { br_free(e); return 1; }
    if (br_scenes_depth(&e->scenes) == 0) rc = 1;              /* the scene failed to load */
    else if (g_headless) {
        br_vec2 before, after;
        br_engine_frame(e);
        before = first_moving_position(e);
        for (i = 0; i < 120; ++i) br_engine_frame(e);
        after = first_moving_position(e);
        br_image_save_bmp(&e->framebuffer, "sceneplayer.bmp");
        {
            const br_world *w = &br_scenes_top(&e->scenes)->world;
            uint32_t errors = br_script_error_count(w);
            rc = br_v2_dist(before, after) > 1 && errors == 0 ? 0 : 1;
            printf("sceneplayer (headless): %s, moved %.0f units in 2 s, %u script error%s, wrote sceneplayer.bmp\n", g_title,
                   (double)br_v2_dist(before, after), errors, errors == 1 ? "" : "s");
            if (errors) printf("  last: %s\n", br_script_last_error(w));
        }
    } else {
        while (br_engine_frame(e)) { /* the scene's systems do everything */ }
    }
    br_engine_shutdown(e);
    br_free(e);
    return rc;
}
