/* Tests: rules of the Pop Defense sample game (examples/towerdefense/td_*.c). */
#include "test.h"
#include "td_internal.h"
#include <string.h>

static td_game G;     /* ~350 KB: keep off the stack */
static td_game G2;

static int count_events(const td_game *g, td_event_type type) {
    int i, n = 0;
    for (i = 0; i < g->event_count; ++i) n += g->events[i].type == type;
    return n;
}

static int count_balloons(const td_game *g, td_balloon_type type) {
    int i, n = 0;
    for (i = 0; i < g->balloon_count; ++i) n += g->balloons[i].alive && g->balloons[i].type == type;
    return n;
}

/* A fresh game with nothing on the field and lots of cash. */
static void fresh(void) {
    td_game_init(&G);
    G.cash = 100000;
}

static void test_td_data(void) {
    int t, p, k, round, total = 0;
    CHECK_EQ_INT(td_balloon_type_rbe(TD_RED), 1);
    CHECK_EQ_INT(td_balloon_type_rbe(TD_PINK), 5);
    CHECK_EQ_INT(td_balloon_type_rbe(TD_BLACK), 11);
    CHECK_EQ_INT(td_balloon_type_rbe(TD_LEAD), 23);
    CHECK_EQ_INT(td_balloon_type_rbe(TD_RAINBOW), 47);
    CHECK_EQ_INT(td_balloon_type_rbe(TD_CERAMIC), 104);
    CHECK_EQ_INT(td_balloon_type_rbe(TD_BLIMP), 616);
    for (t = 0; t < TD_TOWER_TYPE_COUNT; ++t) {
        const td_tower_def *d = td_tower_info((td_tower_type)t);
        CHECK(d->cost > 0 && d->base.interval > 0);
        for (p = 0; p < TD_PATHS; ++p)
            for (k = 0; k < TD_TIERS; ++k) {
                CHECK(strlen(d->paths[p][k].name) <= 17);       /* fits the side panel */
                CHECK(d->paths[p][k].cost > 0 && d->paths[p][k].apply != NULL);
            }
    }
    CHECK_EQ_INT(td_round_count(), 30);
    for (round = 1; round <= td_round_count(); ++round) {
        const td_group *groups;
        int n = td_round_groups(round, &groups);
        CHECK(n >= 1 && n <= 16);
        total += n;
    }
    { const td_group *none; CHECK(td_round_groups(31, &none) == 0 && none == NULL); }
    CHECK(total > 30);
}

static void test_td_map(void) {
    td_map *m = &G.map;
    br_vec2 a, b, dir;
    td_game_init(&G);
    CHECK(m->length > 1000.0f);
    CHECK(m->points[0].x < 0);                                        /* enters off screen */
    CHECK(m->points[m->point_count - 1].y > TD_MAP_H);                /* and leaves off screen */
    a = td_map_point(m, -5.0f);
    CHECK(a.x == m->points[0].x && a.y == m->points[0].y);
    b = td_map_point(m, m->length + 10.0f);
    CHECK(b.x == m->points[m->point_count - 1].x);
    a = td_map_point(m, 300.0f);
    b = td_map_point(m, 301.0f);
    CHECK_NEAR(br_v2_dist(a, b), 1.0, 0.05);                         /* distance is arc length */
    dir = td_map_direction(m, 300.0f);
    CHECK_NEAR(br_v2_len(dir), 1.0, 1e-4);
    CHECK_NEAR(td_map_distance_to_path(m, a), 0.0, 0.01);
    CHECK(!td_map_buildable(m, a, 9));                                /* on the path */
    CHECK(td_map_buildable(m, br_v2(100, 210), 9));                   /* grass inside the bend */
    CHECK(!td_map_buildable(m, m->obstacles[0].pos, 9));              /* the pond */
    CHECK(!td_map_buildable(m, br_v2(3, 200), 9));                    /* off the edge */
}

static void test_td_popping(void) {
    td_balloon *b;
    int cash;
    fresh();
    cash = G.cash;
    td_balloon_spawn(&G, TD_BLUE, false, 200);
    CHECK_EQ_INT(td_balloon_damage(&G, 0, 1, TD_DMG_SHARP, 7, 0), 1);
    CHECK_EQ_INT(count_balloons(&G, TD_RED), 1);                     /* blue -> red */
    CHECK_EQ_INT(G.cash, cash + 1);
    CHECK(G.balloons[1].shield == 7);                                /* that dart can't hit it again */
    CHECK_EQ_INT(count_events(&G, TD_EV_POP), 1);

    fresh();                                                          /* extra damage carries through */
    td_balloon_spawn(&G, TD_GREEN, true, 200);
    CHECK_EQ_INT(td_balloon_damage(&G, 0, 2, TD_DMG_SHARP, 0, 0), 2);
    CHECK_EQ_INT(count_balloons(&G, TD_RED), 1);
    CHECK_EQ_INT(count_balloons(&G, TD_BLUE), 0);
    CHECK(G.balloons[2].camo);                                       /* children stay camo */

    fresh();                                                          /* lead: sharp bounces off */
    td_balloon_spawn(&G, TD_LEAD, false, 200);
    CHECK_EQ_INT(td_balloon_damage(&G, 0, 5, TD_DMG_SHARP, 0, 0), TD_BLOCKED);
    CHECK(G.balloons[0].alive);
    CHECK_EQ_INT(count_events(&G, TD_EV_BLOCKED), 1);
    CHECK_EQ_INT(td_balloon_damage(&G, 0, 1, TD_DMG_EXPLOSIVE, 0, 0), 1);   /* bombs pop lead */
    CHECK_EQ_INT(count_balloons(&G, TD_BLACK), 2);
    b = &G.balloons[1];
    CHECK_EQ_INT(td_balloon_damage(&G, (int)(b - G.balloons), 1, TD_DMG_EXPLOSIVE, 0, 0), TD_BLOCKED);  /* black: bomb-proof */
    CHECK(!td_balloon_hurt_by(&G.balloons[1], TD_DMG_EXPLOSIVE));
    CHECK(td_balloon_hurt_by(&G.balloons[1], TD_DMG_NORMAL));

    fresh();                                                          /* ceramic shell takes 10 hits */
    td_balloon_spawn(&G, TD_CERAMIC, false, 200);
    {
        int i;
        for (i = 0; i < 9; ++i) td_balloon_damage(&G, 0, 1, TD_DMG_SHARP, 0, 0);
        CHECK(G.balloons[0].alive && G.balloons[0].hp == 1);
        CHECK_EQ_INT(td_balloon_rbe(&G.balloons[0]), 104 - 9);
        CHECK_EQ_INT(count_events(&G, TD_EV_HIT), 9);
        td_balloon_damage(&G, 0, 1, TD_DMG_SHARP, 0, 0);
        CHECK(!G.balloons[0].alive);
        CHECK_EQ_INT(count_balloons(&G, TD_RAINBOW), 2);
    }
    td_balloons_remove_dead(&G);
    CHECK_EQ_INT(G.balloon_count, 2);
    CHECK(G.balloons[0].dist < G.balloons[1].dist);                  /* children spread along the path */
}

static void test_td_movement_and_leaks(void) {
    int i;
    fresh();
    td_balloon_spawn(&G, TD_RED, false, 0);
    td_game_step(&G);
    CHECK_NEAR(G.balloons[0].dist, td_balloon_info(TD_RED)->speed * TD_STEP, 1e-4);
    G.balloons[0].frozen = 0.5f;
    td_game_step(&G);
    CHECK_NEAR(G.balloons[0].dist, td_balloon_info(TD_RED)->speed * TD_STEP, 1e-4);   /* frozen: still */

    fresh();
    td_balloon_spawn(&G, TD_RED, false, G.map.length - 0.5f);
    td_balloon_spawn(&G, TD_CERAMIC, false, G.map.length - 0.5f);
    G.balloons[1].hp = 4;                                             /* a damaged ceramic */
    td_game_step(&G);
    CHECK_EQ_INT(G.balloon_count, 0);
    CHECK_EQ_INT(G.lives, TD_START_LIVES - 1 - (104 - 6));
    CHECK_EQ_INT(count_events(&G, TD_EV_LEAK), 2);

    fresh();                                                          /* losing */
    for (i = 0; i < 2; ++i) td_balloon_spawn(&G, TD_BLIMP, false, G.map.length - 0.1f);
    td_game_step(&G);
    CHECK(G.phase == TD_PHASE_DEFEAT && G.lives == 0);
    CHECK_EQ_INT(count_events(&G, TD_EV_DEFEAT), 1);
    CHECK(td_start_round(&G) == TD_ERR_PHASE);
    CHECK(td_place_tower(&G, TD_DART, br_v2(100, 210)) == TD_ERR_PHASE);
}

static void test_td_build_upgrade_sell(void) {
    td_tower *t;
    td_game_init(&G);
    CHECK(td_can_place(&G, TD_DART, td_map_point(&G.map, 400)) == TD_ERR_BLOCKED);
    CHECK(td_place_tower(&G, TD_DART, br_v2(100, 210)) == TD_OK);
    CHECK_EQ_INT(G.cash, TD_START_CASH - 200);
    CHECK(td_place_tower(&G, TD_DART, br_v2(105, 212)) == TD_ERR_BLOCKED);   /* overlaps */
    G.cash = 100;
    CHECK(td_place_tower(&G, TD_BOMB, br_v2(300, 214)) == TD_ERR_CASH);
    CHECK(td_can_place(&G, TD_BOMB, td_map_point(&G.map, 400)) == TD_ERR_BLOCKED);   /* blocked beats broke */
    CHECK_EQ_INT(td_tower_at(&G, br_v2(103, 208)), 0);
    CHECK_EQ_INT(td_tower_at(&G, br_v2(300, 300)), -1);
    CHECK_EQ_INT(count_events(&G, TD_EV_TOWER_PLACED), 1);

    t = &G.towers[0];
    G.cash = 100000;
    CHECK_EQ_INT(t->stats.pierce, 2);
    CHECK(td_upgrade_tower(&G, 0, 0) == TD_OK);                    /* sharp darts */
    CHECK_EQ_INT(t->stats.pierce, 3);
    CHECK(td_upgrade_tower(&G, 0, 1) == TD_OK);                    /* quick shots */
    CHECK_NEAR(t->stats.interval, td_tower_info(TD_DART)->base.interval * 0.85f, 1e-5);
    CHECK(!t->stats.camo);
    CHECK(td_upgrade_tower(&G, 0, 1) == TD_OK);                    /* eagle eye: camo */
    CHECK(t->stats.camo);
    CHECK(td_upgrade_tower(&G, 0, 1) == TD_OK);                    /* triple shot: path 2 at 3 */
    CHECK_EQ_INT(t->stats.count, 3);
    CHECK(td_upgrade_tower(&G, 0, 1) == TD_ERR_MAXED);
    CHECK(td_upgrade_tower(&G, 0, 0) == TD_OK);                    /* path 1 can reach 2 ... */
    CHECK(td_can_upgrade(&G, 0, 0) == TD_ERR_PATH_LOCKED);         /* ... but not 3 */
    CHECK_EQ_INT(t->tier[0], 2);
    CHECK_EQ_INT(t->stats.pierce, 6);                              /* both paths' upgrades apply */
    CHECK_EQ_INT(t->spent, 200 + 140 + 170 + 100 + 120 + 400);
    {
        int cash = G.cash, refund = td_sell_tower(&G, 0);
        CHECK_EQ_INT(refund, (200 + 140 + 170 + 100 + 120 + 400) * TD_SELL_PERCENT / 100);
        CHECK_EQ_INT(G.cash, cash + refund);
        CHECK_EQ_INT(G.tower_count, 0);
        CHECK_EQ_INT(td_sell_tower(&G, 0), 0);
    }
    CHECK(td_upgrade_tower(&G, 0, 0) == TD_ERR_NO_TOWER);
    td_place_tower(&G, TD_SNIPER, br_v2(456, 236));
    CHECK(G.towers[0].targeting == TD_TARGET_FIRST);
    td_cycle_targeting(&G, 0, 1);
    CHECK(G.towers[0].targeting == TD_TARGET_LAST);
    td_cycle_targeting(&G, 0, -1);
    td_cycle_targeting(&G, 0, -1);
    CHECK(G.towers[0].targeting == TD_TARGET_CLOSE);
}

/* Place a sniper and two balloons; return which balloon type it shot. */
static int sniper_choice(td_targeting prio, td_balloon_type a, float da, td_balloon_type b, float db) {
    int i;
    fresh();
    td_place_tower(&G, TD_SNIPER, br_v2(456, 236));
    G.towers[0].targeting = prio;
    td_balloon_spawn(&G, a, false, da)->frozen = 10;
    td_balloon_spawn(&G, b, false, db)->frozen = 10;
    td_game_clear_events(&G);
    td_game_step(&G);
    for (i = 0; i < G.event_count; ++i)
        if (G.events[i].type == TD_EV_POP) return G.events[i].kind;
    return -1;
}

static void test_td_targeting(void) {
    CHECK_EQ_INT(sniper_choice(TD_TARGET_FIRST, TD_RED, 300, TD_GREEN, 200), TD_RED);
    CHECK_EQ_INT(sniper_choice(TD_TARGET_LAST, TD_RED, 300, TD_GREEN, 200), TD_GREEN);
    CHECK_EQ_INT(sniper_choice(TD_TARGET_STRONG, TD_RED, 300, TD_GREEN, 200), TD_GREEN);
    CHECK_EQ_INT(sniper_choice(TD_TARGET_CLOSE, TD_RED, 120, TD_GREEN, 1200), TD_GREEN);   /* nearer the sniper */

    /* camo: a dart can't see it, a sniper can */
    fresh();
    td_place_tower(&G, TD_DART, br_v2(100, 210));
    td_balloon_spawn(&G, TD_RED, true, 0);
    G.balloons[0].dist = 940;                                        /* left bend, in range */
    G.balloons[0].frozen = 10;
    td_game_step(&G);
    CHECK_EQ_INT(count_events(&G, TD_EV_ATTACK), 0);
    G.towers[0].stats.camo = true;
    td_game_step(&G);
    CHECK_EQ_INT(count_events(&G, TD_EV_ATTACK), 1);
    CHECK(G.projectile_count == 1);
}

static void run_steps(int n) { int i; for (i = 0; i < n; ++i) td_game_step(&G); }

static void test_td_attacks(void) {
    int i;
    /* darts fly, pop and respect pierce */
    fresh();
    td_place_tower(&G, TD_DART, br_v2(100, 210));
    for (i = 0; i < 5; ++i) td_balloon_spawn(&G, TD_RED, false, 940.0f + (float)i)->frozen = 10;
    run_steps(30);
    CHECK_EQ_INT(count_events(&G, TD_EV_ATTACK), 1);                 /* 0.8 s between throws */
    CHECK_EQ_INT(count_events(&G, TD_EV_POP), 2);                    /* pierce 2 */
    CHECK_EQ_INT(G.towers[0].pops, 2);

    /* bombs: splash, pop lead, not black */
    fresh();
    td_place_tower(&G, TD_BOMB, br_v2(100, 210));
    td_balloon_spawn(&G, TD_LEAD, false, 940)->frozen = 10;
    td_balloon_spawn(&G, TD_RED, false, 944)->frozen = 10;
    td_balloon_spawn(&G, TD_BLACK, false, 948)->frozen = 10;
    run_steps(40);
    CHECK_EQ_INT(count_events(&G, TD_EV_EXPLOSION), 1);
    CHECK_EQ_INT(count_balloons(&G, TD_LEAD), 0);
    CHECK_EQ_INT(count_balloons(&G, TD_RED), 0);
    CHECK_EQ_INT(count_balloons(&G, TD_BLACK), 3);                   /* the original + 2 from lead */
    CHECK(count_events(&G, TD_EV_BLOCKED) >= 1);

    /* ice: freezes what it can, leaves white alone */
    fresh();
    td_place_tower(&G, TD_ICE, br_v2(100, 210));
    td_balloon_spawn(&G, TD_RED, false, 940);
    td_balloon_spawn(&G, TD_WHITE, false, 940);
    td_game_step(&G);
    CHECK_EQ_INT(count_events(&G, TD_EV_PULSE), 1);
    CHECK(G.balloons[0].frozen > 1.0f);
    CHECK(G.balloons[1].frozen == 0);
    {
        float d0 = G.balloons[0].dist, d1 = G.balloons[1].dist;
        run_steps(10);
        CHECK(G.balloons[0].dist == d0);
        CHECK(G.balloons[1].dist > d1);
    }

    /* tack: 8 tacks in every direction */
    fresh();
    td_place_tower(&G, TD_TACK, br_v2(100, 210));
    td_balloon_spawn(&G, TD_RED, false, 940)->frozen = 10;
    td_game_step(&G);
    CHECK_EQ_INT(G.projectile_count, 8);

    /* ring of fire pops everything in range at once, lead included */
    fresh();
    td_place_tower(&G, TD_TACK, br_v2(100, 210));
    td_upgrade_tower(&G, 0, 1); td_upgrade_tower(&G, 0, 1); td_upgrade_tower(&G, 0, 1);
    CHECK(G.towers[0].stats.attack == TD_ATK_PULSE);
    td_balloon_spawn(&G, TD_LEAD, false, 940)->frozen = 10;
    td_balloon_spawn(&G, TD_RED, false, 960)->frozen = 10;
    td_game_step(&G);
    CHECK_EQ_INT(count_balloons(&G, TD_LEAD), 0);
    CHECK_EQ_INT(count_balloons(&G, TD_RED), 0);
}

static void test_td_rounds(void) {
    int steps = 0;
    td_game_init(&G);
    CHECK(G.phase == TD_PHASE_BUILD);
    CHECK(td_start_round(&G) == TD_OK);
    CHECK(G.phase == TD_PHASE_ROUND && G.round == 1);
    CHECK(td_start_round(&G) == TD_ERR_PHASE);
    while (G.phase == TD_PHASE_ROUND && steps < 60 * 120) { td_game_step(&G); td_game_clear_events(&G); ++steps; }
    CHECK(G.phase == TD_PHASE_BUILD);
    CHECK_EQ_INT(G.lives, TD_START_LIVES - 20);                      /* round 1: 20 reds, no towers */
    CHECK_EQ_INT(G.cash, TD_START_CASH + 100 + 5 * 1);              /* round bonus */
    CHECK_EQ_INT(G.leaked, 20);
}

static uint64_t game_hash(const td_game *g) {
    uint64_t h = 1469598103934665603ull;
    const unsigned char *p = (const unsigned char *)&g->towers;
    size_t i;
    h = (h ^ (uint64_t)g->cash) * 1099511628211ull;
    h = (h ^ (uint64_t)g->lives) * 1099511628211ull;
    h = (h ^ (uint64_t)g->pops) * 1099511628211ull;
    h = (h ^ (uint64_t)g->steps) * 1099511628211ull;
    for (i = 0; i < sizeof(g->towers[0]) * (size_t)g->tower_count; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

static void play_full_game(td_game *g, bool build) {
    td_autopilot pilot;
    long steps = 0;
    td_game_init(g);
    td_autopilot_init(&pilot);
    while (!td_game_over(g) && steps < 60L * 60 * 30) {
        if (build) td_autopilot_play(&pilot, g);
        else if (g->phase == TD_PHASE_BUILD) td_start_round(g);
        td_game_step(g);
        td_game_clear_events(g);
        ++steps;
    }
}

/* The scripted player beats all 30 rounds; doing nothing loses early. */
static void test_td_full_game(void) {
    play_full_game(&G, true);
    CHECK(G.phase == TD_PHASE_VICTORY);
    CHECK_EQ_INT(G.round, 30);
    CHECK(G.lives > 20);
    CHECK(G.tower_count >= 9);
    CHECK(G.events_dropped == 0);
    play_full_game(&G2, true);                                        /* deterministic */
    CHECK(game_hash(&G) == game_hash(&G2));
    printf("    autopilot: victory with %d lives, %d pops, %d towers\n", G.lives, G.pops, G.tower_count);

    play_full_game(&G, false);
    CHECK(G.phase == TD_PHASE_DEFEAT);
    CHECK(G.round <= 6);
}

void suite_towerdefense(void) {
    RUN(test_td_data);
    RUN(test_td_map);
    RUN(test_td_popping);
    RUN(test_td_movement_and_leaks);
    RUN(test_td_build_upgrade_sell);
    RUN(test_td_targeting);
    RUN(test_td_attacks);
    RUN(test_td_rounds);
    RUN(test_td_full_game);
}
