/* Recording and replaying a run (replay.h), with Pantry Quest: a run is
 * recorded through the replay platform (keys from the program), then played
 * back from the file and checked picture by picture. */
#include "test.h"

#include <string.h>

static char g_scene[BR_PATH_MAX];

static bool rp_enter(br_scene *s) {
    char path[BR_PATH_MAX * 2], err[256];
    br_builtin_register(&s->world, NULL);
    snprintf(path, sizeof(path), "examples/pantryquest/%s", g_scene);
    return br_world_load_file(&s->world, path, NULL, 0, err, sizeof(err)) > 0;
}
static const br_scene_desc *rp_desc(void);
static void rp_update(br_scene *s, float dt) {
    char next[BR_PATH_MAX];
    BR_UNUSED(dt);
    if (br_script_scene_request(&s->world, next, sizeof(next))) {
        snprintf(g_scene, sizeof(g_scene), "%s", next);
        br_scene_switch(&s->engine->scenes, rp_desc());
    }
}
static const br_scene_desc k_rp_scene = { "game", 0, false, false, rp_enter, NULL, NULL, NULL, NULL, rp_update, NULL, NULL };
static const br_scene_desc *rp_desc(void) { return &k_rp_scene; }

static void rp_restart(br_engine *e) { BR_UNUSED(e); snprintf(g_scene, sizeof(g_scene), "title.scene"); }

static br_engine *rp_start(void) {
    br_engine_config cfg = br_engine_default_config();
    br_engine *e = BR_NEW(br_engine);
    snprintf(g_scene, sizeof(g_scene), "title.scene");
    br_assets_set_root("examples/pantryquest/");
    br_script_set_strict(true);
    br_save_set_folder(test_path("replay_saves"));
    br_replay_clear_saves();
    cfg.width = 256;
    cfg.height = 240;
    cfg.audio_sample_rate = 0;
    cfg.initial_scene = &k_rp_scene;
    cfg.pause_when_minimized = false;
    cfg.platform = br_replay_platform(br_platform_headless(), false);
    if (!e || !br_engine_init(e, &cfg)) { br_free(e); return NULL; }
    br_replay_attach(e);
    return e;
}

static void rp_stop(br_engine *e) {
    br_engine_shutdown(e);
    br_free(e);
    br_assets_clear();
    br_assets_set_root("");
    br_script_set_strict(false);
    br_save_set_folder(NULL);
}

static void frames(br_engine *e, int n) { while (n-- > 0) br_engine_frame(e); }

/* Title, a new game in file 1, then a walk about the first screen and a poke. */
static bool record_run(const char *path, uint64_t *length, uint32_t *last_hash) {
    br_engine *e;
    if (!br_replay_record("test", 10)) return false;
    if (!(e = rp_start())) return false;
    br_replay_segment("Title");
    frames(e, 30);
    br_replay_key(BR_KEY_ENTER, true);  frames(e, 1); br_replay_key(BR_KEY_ENTER, false); frames(e, 20);
    br_replay_key(BR_KEY_ENTER, true);  frames(e, 1); br_replay_key(BR_KEY_ENTER, false); frames(e, 60);
    br_replay_segment("Walk");
    br_replay_key(BR_KEY_LEFT, true);   frames(e, 40);
    br_replay_key(BR_KEY_UP, true);     frames(e, 25);    /* (a diagonal) */
    br_replay_key(BR_KEY_LEFT, false);  frames(e, 20);
    br_replay_key(BR_KEY_UP, false);
    br_replay_key(BR_KEY_RIGHT, true);  frames(e, 50);
    br_replay_key(BR_KEY_RIGHT, false); frames(e, 5);
    br_replay_key(BR_KEY_X, true);      frames(e, 1);
    br_replay_key(BR_KEY_X, false);     frames(e, 40);
    *length = br_replay_frame();
    br_replay_draw_every_frame(true);
    frames(e, 1);                                         /* (one more, drawn, for its picture) */
    br_replay_draw_every_frame(false);
    *length = br_replay_frame();
    *last_hash = br_replay_last_hash();
    CHECK_EQ_INT(br_script_error_count(&br_scenes_top(&e->scenes)->world), 0);
    CHECK(br_replay_finish(path));
    rp_stop(e);
    return true;
}

/* Plays a recording to its end, headless. Returns the hash of the last frame. */
static uint32_t play_back(const char *path, int segment, uint32_t *bad, uint32_t *checked) {
    br_engine *e;
    uint32_t last;
    if (!br_replay_load(path)) return 0;
    if (segment > 0) br_replay_start_at_segment(segment);
    br_replay_set_restart_hook(rp_restart);
    if (!(e = rp_start())) return 0;
    while (br_engine_frame(e) && br_replay_after_frame(e)) {
        br_event stray = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_ESCAPE, 0, false);
        br_headless_inject(&stray);                      /* real keys never reach the game */
    }
    *bad = br_replay_mismatches(checked, NULL);
    last = br_replay_last_hash();
    CHECK(br_replay_ended());
    rp_stop(e);
    br_replay_finish(test_path("unused.brec"));          /* (forgets it) */
    return last;
}

static void test_replay_round_trip(void) {
    uint64_t length = 0;
    uint32_t last = 0, bad = 1, checked = 0, again;
    const char *path = test_path("run.brec");
    CHECK(record_run(path, &length, &last));
    CHECK(length > 250);
    again = play_back(path, 0, &bad, &checked);
    CHECK_EQ_INT(bad, 0);
    CHECK(checked >= length / 10 - 1);
    CHECK_EQ_INT(again, last);                           /* the very last picture, too */
    /* starting from a later segment (the earlier frames run unseen) ends the same */
    again = play_back(path, 1, &bad, &checked);
    CHECK_EQ_INT(bad, 0);
    CHECK_EQ_INT(again, last);
}

static void test_replay_file(void) {
    const char *text =
        "brec 1\ngame Test\nseg 0 Title\nk 5 +257\nh 9 0000beef\nk 6 -257\nseg 40 Second part, with spaces\nend 50\n";
    CHECK(br_replay_load_text(text, strlen(text)));
    CHECK_EQ_INT(br_replay_length(), 50);
    CHECK_EQ_INT(br_replay_segment_count(), 2);
    CHECK(strcmp(br_replay_segment_name(1), "Second part, with spaces") == 0);
    CHECK_EQ_INT(br_replay_segment_start(1), 40);
    CHECK_EQ_INT(br_replay_segment_at(39), 0);
    CHECK_EQ_INT(br_replay_segment_at(40), 1);
    QUIET(CHECK(!br_replay_load_text("not a recording", 15)));
    br_replay_finish(test_path("unused.brec"));
}

/* A recording that doesn't match what the game does (a key changed) is caught. */
static void test_replay_drift(void) {
    uint64_t length = 0;
    uint32_t last = 0, bad = 0, checked = 0;
    size_t size;
    const char *path = test_path("run2.brec"), *bent = test_path("bent.brec");
    char *text, *k;
    CHECK(record_run(path, &length, &last));
    text = (char *)br_file_read_all(path, &size);
    CHECK(text != NULL);
    if (!text) return;
    k = strstr(text, " +263");                           /* the walk left becomes a walk up */
    CHECK(k != NULL);
    if (k) memcpy(k, " +265", 5);
    CHECK(br_file_write_all(bent, text, size));
    br_free(text);
    QUIET(play_back(bent, 0, &bad, &checked));
    CHECK(bad > 0);
}

/* Watched (live), with the playback keys: pausing, a frame at a time, a
 * faster speed, and going back to the start of a segment - the run still
 * matches the recording frame for frame. */
static void tap(br_engine *e, br_key k) {
    br_event ev = br_event_key(BR_EVENT_KEY_DOWN, k, 0, false);
    br_headless_inject(&ev);
    br_engine_frame(e);
    br_replay_after_frame(e);
    ev = br_event_key(BR_EVENT_KEY_UP, k, 0, false);
    br_headless_inject(&ev);
}
static void test_replay_controls(void) {
    uint64_t length = 0, at;
    uint32_t last = 0, bad = 1, checked = 0;
    const char *path = test_path("run3.brec");
    br_engine *e;
    br_engine_config cfg;
    int i;
    CHECK(record_run(path, &length, &last));
    CHECK(br_replay_load(path));
    br_replay_set_restart_hook(rp_restart);
    snprintf(g_scene, sizeof(g_scene), "title.scene");
    br_assets_set_root("examples/pantryquest/");
    br_script_set_strict(true);
    br_save_set_folder(test_path("replay_saves"));
    br_replay_clear_saves();
    cfg = br_engine_default_config();
    cfg.width = 256;
    cfg.height = 240;
    cfg.audio_sample_rate = 0;
    cfg.initial_scene = &k_rp_scene;
    cfg.pause_when_minimized = false;
    cfg.platform = br_replay_platform(br_platform_headless(), true);   /* live: the keys work */
    e = BR_NEW(br_engine);
    CHECK(e && br_engine_init(e, &cfg));
    if (!e) return;
    br_replay_attach(e);
    for (i = 0; i < 100; ++i) { br_engine_frame(e); br_replay_after_frame(e); }
    tap(e, BR_KEY_SPACE);                                /* pause */
    at = br_replay_frame();
    for (i = 0; i < 30; ++i) { br_engine_frame(e); br_replay_after_frame(e); }
    CHECK_EQ_INT(br_replay_frame(), at);                 /* nothing moves */
    tap(e, BR_KEY_PERIOD);                               /* one frame */
    br_engine_frame(e); br_replay_after_frame(e);
    CHECK_EQ_INT(br_replay_frame(), at + 1);
    tap(e, BR_KEY_SPACE);                                /* on again, faster */
    tap(e, BR_KEY_3);
    for (i = 0; i < 40; ++i) { br_engine_frame(e); br_replay_after_frame(e); }
    for (i = 0; i < 60; ++i) {                           /* stop and go, often (a pause leaves the run as it was) */
        int k;
        tap(e, BR_KEY_SPACE);
        br_engine_frame(e); br_replay_after_frame(e);
        tap(e, BR_KEY_SPACE);
        for (k = 0; k < 1 + i % 5; ++k) { br_engine_frame(e); br_replay_after_frame(e); }
    }
    CHECK_EQ_INT(br_replay_mismatches(NULL, NULL), 0);
    tap(e, BR_KEY_P);                                    /* back to this segment's start: the game starts again */
    for (i = 0; i < 4000 && !br_replay_ended(); ++i) { br_engine_frame(e); br_replay_after_frame(e); }
    CHECK(br_replay_ended());
    bad = br_replay_mismatches(&checked, NULL);
    CHECK_EQ_INT(bad, 0);
    CHECK(checked > 20);
    CHECK_EQ_INT(br_replay_last_hash(), last);
    rp_stop(e);
    br_replay_finish(test_path("unused.brec"));
}

void suite_replay(void) {
    RUN(test_replay_file);
    RUN(test_replay_round_trip);
    RUN(test_replay_drift);
    RUN(test_replay_controls);
}
