/* Tests: ECS and scenes. */
#include "test.h"
#include <string.h>

typedef struct Position { float x, y; } Position;
typedef struct Velocity { float x, y; } Velocity;
typedef struct Health { int hp; } Health;

/* ======================= ECS ============================================ */

static void test_entity_lifecycle(void) {
    br_world w;
    br_entity a, b, c;
    CHECK(br_world_init(&w));
    a = br_entity_create(&w);
    b = br_entity_create(&w);
    CHECK(a != BR_ENTITY_NULL && b != BR_ENTITY_NULL && a != b);
    CHECK(br_entity_alive(&w, a) && br_entity_alive(&w, b));
    CHECK(!br_entity_alive(&w, BR_ENTITY_NULL));
    CHECK_EQ_INT(br_world_entity_count(&w), 2);
    br_entity_destroy(&w, a);
    CHECK(!br_entity_alive(&w, a));
    c = br_entity_create(&w);                 /* recycles a's index ... */
    CHECK(BR_ENTITY_INDEX(c) == BR_ENTITY_INDEX(a));
    CHECK(c != a);                            /* ... with a new generation */
    CHECK(!br_entity_alive(&w, a));           /* stale handle stays dead */
    br_entity_destroy(&w, a);                 /* destroying a stale handle is a no-op */
    CHECK(br_entity_alive(&w, c));
    CHECK_EQ_INT(br_world_entity_count(&w), 2);
    br_world_destroy(&w);
}

static void test_components(void) {
    br_world w;
    br_component POS, VEL, TAG;
    br_entity e, f;
    Position *p;
    br_world_init(&w);
    POS = BR_COMPONENT(&w, Position);
    VEL = BR_COMPONENT(&w, Velocity);
    TAG = br_component_register(&w, "Player", 0);
    CHECK(BR_COMPONENT(&w, Position) == POS);       /* idempotent */
    CHECK(br_component_find(&w, "Velocity") == VEL);
    e = br_entity_create(&w);
    f = br_entity_create(&w);
    p = BR_ADD(&w, e, POS, Position, { 1, 2 });
    CHECK(p && p->x == 1 && p->y == 2);
    CHECK(br_has(&w, e, POS) && !br_has(&w, e, VEL) && !br_has(&w, f, POS));
    CHECK(br_add(&w, e, TAG, NULL) != NULL && br_has(&w, e, TAG));
    BR_ADD(&w, e, POS, Position, { 5, 6 });          /* overwrite */
    CHECK(BR_GET(&w, e, POS, Position)->x == 5);
    CHECK_EQ_INT(br_component_count(&w, POS), 1);
    CHECK(br_remove(&w, e, POS));
    CHECK(!br_remove(&w, e, POS));
    CHECK(BR_GET(&w, e, POS, Position) == NULL);
    CHECK(br_add(&w, BR_ENTITY_NULL, POS, NULL) == NULL);
    br_entity_destroy(&w, e);
    CHECK(br_get(&w, e, TAG) == NULL);
    CHECK_EQ_INT(br_component_count(&w, TAG), 0);
    br_world_destroy(&w);
}

static void test_swap_remove_integrity(void) {
    br_world w;
    br_component HP;
    br_entity ents[2000];
    int i, ok = 1;
    br_world_init(&w);
    HP = BR_COMPONENT(&w, Health);
    for (i = 0; i < 2000; ++i) {
        ents[i] = br_entity_create(&w);
        BR_ADD(&w, ents[i], HP, Health, { i });
    }
    for (i = 0; i < 2000; i += 3) br_entity_destroy(&w, ents[i]);
    for (i = 0; i < 2000; ++i) {
        Health *h = BR_GET(&w, ents[i], HP, Health);
        if (i % 3 == 0) ok &= h == NULL;
        else ok &= h != NULL && h->hp == i;
    }
    CHECK(ok);
    CHECK_EQ_INT(br_component_count(&w, HP), 2000 - 667);
    br_world_destroy(&w);
}

static void test_queries(void) {
    br_world w;
    br_component POS, VEL, HP;
    br_component pv[2];
    br_query q;
    int i, n = 0, ok = 1;
    br_world_init(&w);
    POS = BR_COMPONENT(&w, Position);
    VEL = BR_COMPONENT(&w, Velocity);
    HP = BR_COMPONENT(&w, Health);
    pv[0] = POS; pv[1] = VEL;
    for (i = 0; i < 100; ++i) {
        br_entity e = br_entity_create(&w);
        BR_ADD(&w, e, POS, Position, { (float)i, 0 });
        if (i % 2 == 0) BR_ADD(&w, e, VEL, Velocity, { 1, 2 });
        if (i % 4 == 0) BR_ADD(&w, e, HP, Health, { 10 });
    }
    q = br_query_make(&w, pv, 2);
    while (br_query_next(&q)) {
        Position *p = BR_QUERY_GET(&q, 0, Position);
        Velocity *v = BR_QUERY_GET(&q, 1, Velocity);
        p->x += v->x;
        p->y += v->y;
        ok &= br_get(&w, q.entity, POS) == p;
        n++;
    }
    CHECK(ok);
    CHECK_EQ_INT(n, 50);
    CHECK_EQ_INT(q.driver, VEL);    /* smallest pool drives iteration */
    q = br_query_make(&w, pv, 2);
    br_query_exclude(&q, HP);
    CHECK_EQ_INT(br_query_count(q), 25);
    q = br_query_make(&w, &POS, 1);
    n = 0;
    while (br_query_next(&q)) if (BR_QUERY_GET(&q, 0, Position)->y == 2) n++;
    CHECK_EQ_INT(n, 50);
    br_world_destroy(&w);
}

static void test_destroy_while_iterating(void) {
    br_world w;
    br_component HP;
    br_query q;
    int i, visited = 0;
    br_world_init(&w);
    HP = BR_COMPONENT(&w, Health);
    for (i = 0; i < 64; ++i) BR_ADD(&w, br_entity_create(&w), HP, Health, { i });
    q = br_query_make(&w, &HP, 1);
    while (br_query_next(&q)) {
        visited++;
        if (BR_QUERY_GET(&q, 0, Health)->hp % 2) br_entity_destroy(&w, q.entity);
    }
    CHECK_EQ_INT(visited, 64);                   /* every entity seen exactly once */
    CHECK_EQ_INT(br_world_entity_count(&w), 32);
    /* deferred destruction */
    q = br_query_make(&w, &HP, 1);
    while (br_query_next(&q)) br_entity_destroy_deferred(&w, q.entity);
    CHECK_EQ_INT(br_world_entity_count(&w), 32);
    br_world_flush(&w);
    CHECK_EQ_INT(br_world_entity_count(&w), 0);
    br_world_destroy(&w);
}

typedef struct hook_counts { int added, removed, sum_removed; } hook_counts;
static void on_add_hp(br_world *w, br_entity e, void *d, void *u) {
    BR_UNUSED(w); BR_UNUSED(e); BR_UNUSED(d); ((hook_counts *)u)->added++;
}
static void on_remove_hp(br_world *w, br_entity e, void *d, void *u) {
    BR_UNUSED(w); BR_UNUSED(e);
    ((hook_counts *)u)->removed++;
    ((hook_counts *)u)->sum_removed += ((Health *)d)->hp;
}

static void test_hooks_and_clear(void) {
    br_world w;
    br_component HP;
    hook_counts hc;
    br_entity a, b;
    memset(&hc, 0, sizeof(hc));
    br_world_init(&w);
    HP = BR_COMPONENT(&w, Health);
    br_component_set_hooks(&w, HP, on_add_hp, on_remove_hp, &hc);
    a = br_entity_create(&w); b = br_entity_create(&w);
    BR_ADD(&w, a, HP, Health, { 3 });
    BR_ADD(&w, b, HP, Health, { 4 });
    BR_ADD(&w, b, HP, Health, { 5 });   /* overwrite: no extra add hook */
    CHECK_EQ_INT(hc.added, 2);
    br_remove(&w, a, HP);
    br_world_clear(&w);                 /* removes b's component via destroy */
    CHECK_EQ_INT(hc.removed, 2);
    CHECK_EQ_INT(hc.sum_removed, 8);
    CHECK_EQ_INT(br_world_entity_count(&w), 0);
    CHECK(br_component_find(&w, "Health") == HP);  /* types survive clear */
    br_world_destroy(&w);
}

typedef struct sys_log { char order[64]; } sys_log;
static void sys_x(br_world *w, float dt, void *u) { BR_UNUSED(w); BR_UNUSED(dt); strcat(((sys_log *)u)->order, "x"); }
static void sys_y(br_world *w, float dt, void *u) { BR_UNUSED(w); BR_UNUSED(dt); strcat(((sys_log *)u)->order, "y"); }
static void sys_z(br_world *w, float dt, void *u) { BR_UNUSED(w); BR_UNUSED(dt); strcat(((sys_log *)u)->order, "z"); }

static void test_systems(void) {
    br_world w;
    sys_log l;
    br_system sy;
    memset(&l, 0, sizeof(l));
    br_world_init(&w);
    br_system_add(&w, "z", BR_PHASE_RENDER, sys_z, &l, 0);
    sy = br_system_add(&w, "y", BR_PHASE_UPDATE, sys_y, &l, 10);
    br_system_add(&w, "x", BR_PHASE_UPDATE, sys_x, &l, -5);
    br_world_run_phase(&w, BR_PHASE_UPDATE, 0.016f);
    br_world_run_phase(&w, BR_PHASE_RENDER, 0);
    CHECK(strcmp(l.order, "xyz") == 0);
    br_system_set_enabled(&w, sy, false);
    br_world_run_phase(&w, BR_PHASE_UPDATE, 0.016f);
    CHECK(strcmp(l.order, "xyzx") == 0);
    br_system_remove(&w, sy);
    CHECK_EQ_INT(w.system_count, 2);
    br_world_destroy(&w);
}

static void test_many_entities(void) {
    br_world w;
    br_component POS, VEL, pv[2];
    br_query q;
    int i, n = 0;
    br_world_init(&w);
    POS = BR_COMPONENT(&w, Position);
    VEL = BR_COMPONENT(&w, Velocity);
    pv[0] = POS; pv[1] = VEL;
    for (i = 0; i < 100000; ++i) {
        br_entity e = br_entity_create(&w);
        BR_ADD(&w, e, POS, Position, { 0, 0 });
        BR_ADD(&w, e, VEL, Velocity, { 1, 1 });
    }
    q = br_query_make(&w, pv, 2);
    while (br_query_next(&q)) { BR_QUERY_GET(&q, 0, Position)->x += 1; n++; }
    CHECK_EQ_INT(n, 100000);
    CHECK_EQ_INT(br_world_entity_count(&w), 100000);
    br_world_destroy(&w);
}

void suite_ecs(void) {
    RUN(test_entity_lifecycle);
    RUN(test_components);
    RUN(test_swap_remove_integrity);
    RUN(test_queries);
    RUN(test_destroy_while_iterating);
    RUN(test_hooks_and_clear);
    RUN(test_systems);
    RUN(test_many_entities);
}

/* ======================= scenes ========================================= */

static char g_trace[512];
static void trace(const char *scene, const char *what) {
    strcat(g_trace, scene); strcat(g_trace, "."); strcat(g_trace, what); strcat(g_trace, " ");
}

typedef struct game_data { int updates, fixed; br_entity player; } game_data;

static bool game_enter(br_scene *s) {
    game_data *d = (game_data *)s->data;
    br_component POS = BR_COMPONENT(&s->world, Position);
    trace("G", "enter");
    d->player = br_entity_create(&s->world);
    BR_ADD(&s->world, d->player, POS, Position, { 1, 1 });
    return true;
}
static void game_exit(br_scene *s)   { BR_UNUSED(s); trace("G", "exit"); }
static void game_pause(br_scene *s)  { BR_UNUSED(s); trace("G", "pause"); }
static void game_resume(br_scene *s) { BR_UNUSED(s); trace("G", "resume"); }
static void game_update(br_scene *s, float dt) { BR_UNUSED(dt); ((game_data *)s->data)->updates++; }
static void game_fixed(br_scene *s, float dt) { BR_UNUSED(dt); ((game_data *)s->data)->fixed++; }
static void game_render(br_scene *s) { BR_UNUSED(s); trace("G", "render"); }
static bool game_event(br_scene *s, const br_event *e) { BR_UNUSED(s); BR_UNUSED(e); trace("G", "event"); return true; }

static const br_scene_desc k_game = {
    "game", sizeof(game_data), false, false,
    game_enter, game_exit, game_pause, game_resume, game_fixed, game_update, game_render, game_event
};

static bool menu_enter(br_scene *s) { BR_UNUSED(s); trace("M", "enter"); return true; }
static void menu_exit(br_scene *s) { BR_UNUSED(s); trace("M", "exit"); }
static void menu_render(br_scene *s) { BR_UNUSED(s); trace("M", "render"); }
static bool menu_event(br_scene *s, const br_event *e) { BR_UNUSED(s); trace("M", "event"); return e->type == BR_EVENT_KEY_DOWN; }

/* Transparent pause overlay that lets updates/events through. */
static const br_scene_desc k_overlay = {
    "overlay", 0, true, true, menu_enter, menu_exit, NULL, NULL, NULL, NULL, menu_render, menu_event
};
/* Opaque modal menu. */
static const br_scene_desc k_modal = {
    "modal", 0, false, false, menu_enter, menu_exit, NULL, NULL, NULL, NULL, menu_render, menu_event
};

static bool refuse_enter(br_scene *s) { BR_UNUSED(s); trace("R", "refuse"); return false; }
static const br_scene_desc k_refuse = { "refuse", 64, false, false, refuse_enter, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

static void test_scene_stack_lifecycle(void) {
    br_scene_manager m;
    br_events ev;
    br_event out;
    game_data *gd;
    int changed = 0;
    br_events_init(&ev, 16);
    br_scenes_init(&m, NULL, &ev);
    g_trace[0] = 0;

    br_scene_push(&m, &k_game);
    CHECK_EQ_INT(br_scenes_depth(&m), 0);       /* deferred until apply */
    CHECK(br_scenes_apply(&m));
    CHECK_EQ_INT(br_scenes_depth(&m), 1);
    gd = (game_data *)br_scenes_top(&m)->data;
    CHECK(gd != NULL && br_entity_alive(&br_scenes_top(&m)->world, gd->player));
    CHECK(br_scenes_top(&m)->world.user == br_scenes_top(&m));

    br_scene_push(&m, &k_overlay);
    br_scenes_apply(&m);
    br_scenes_update(&m, 0.1f);                 /* overlay has update_below */
    br_scenes_fixed_update(&m, 0.1f);
    CHECK_EQ_INT(gd->updates, 1);
    CHECK_EQ_INT(gd->fixed, 1);
    br_scenes_render(&m);                       /* both render, bottom first */
    {   br_event k = br_event_key(BR_EVENT_MOUSE_MOVE, BR_KEY_A, 0, false);
        CHECK(br_scenes_event(&m, &k)); }      /* overlay passes, game consumes */
    br_scene_pop(&m);
    br_scenes_apply(&m);
    CHECK(strcmp(g_trace, "G.enter G.pause M.enter G.render M.render M.event G.event M.exit G.resume ") == 0);
    if (strcmp(g_trace, "G.enter G.pause M.enter G.render M.render M.event G.event M.exit G.resume ") != 0)
        fprintf(stderr, "      trace: %s\n", g_trace);

    g_trace[0] = 0;
    br_scene_push(&m, &k_modal);
    br_scenes_apply(&m);
    br_scenes_update(&m, 0.1f);                 /* modal blocks updates below */
    CHECK_EQ_INT(gd->updates, 1);
    br_scenes_render(&m);                       /* opaque: only modal renders */
    {   br_event k = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_A, 0, false);
        CHECK(br_scenes_event(&m, &k)); }
    {   br_event k = br_event_make(BR_EVENT_MOUSE_MOVE);
        CHECK(!br_scenes_event(&m, &k)); }      /* not consumed, does not reach game */
    br_scene_switch(&m, &k_refuse);            /* modal exits, refuse aborts */
    QUIET(br_scenes_apply(&m));
    CHECK_EQ_INT(br_scenes_depth(&m), 1);
    CHECK(strcmp(g_trace, "G.pause M.enter M.render M.event M.event M.exit R.refuse G.resume ") == 0);
    if (strcmp(g_trace, "G.pause M.enter M.render M.event M.event M.exit R.refuse G.resume ") != 0)
        fprintf(stderr, "      trace: %s\n", g_trace);

    br_scene_clear(&m);
    br_scenes_apply(&m);
    CHECK_EQ_INT(br_scenes_depth(&m), 0);
    CHECK(br_scenes_top(&m) == NULL);
    while (br_events_poll(&ev, &out)) changed += out.type == BR_EVENT_SCENE_CHANGED;
    CHECK_EQ_INT(changed, 6);
    br_scenes_shutdown(&m);
    br_events_destroy(&ev);
}

static void test_scene_ecs_phases(void) {
    br_scene_manager m;
    sys_log l;
    memset(&l, 0, sizeof(l));
    br_scenes_init(&m, NULL, NULL);
    br_scene_push(&m, &k_game);
    br_scenes_apply(&m);
    br_system_add(&br_scenes_top(&m)->world, "z", BR_PHASE_RENDER, sys_z, &l, 0);
    br_system_add(&br_scenes_top(&m)->world, "x", BR_PHASE_FIXED_UPDATE, sys_x, &l, 0);
    br_system_add(&br_scenes_top(&m)->world, "y", BR_PHASE_LATE_UPDATE, sys_y, &l, 0);
    g_trace[0] = 0;
    br_scenes_fixed_update(&m, 0.01f);
    br_scenes_update(&m, 0.01f);
    br_scenes_render(&m);
    CHECK(strcmp(l.order, "xyz") == 0);
    CHECK_NEAR(br_scenes_top(&m)->time, 0.01, 1e-6);
    br_scenes_shutdown(&m);       /* exits and frees everything */
    CHECK(strstr(g_trace, "G.render") != NULL);
}

void suite_scene(void) {
    RUN(test_scene_stack_lifecycle);
    RUN(test_scene_ecs_phases);
}
