/*
 * POP DEFENSE - autopilot: a scripted player.
 *
 * It follows a fixed build order, buying the next item as soon as it can
 * afford it, and starts each round as soon as the last one is cleared. It
 * only uses the public commands from td.h, exactly like a human player.
 * It plays the attract mode behind the title screen and the headless demo,
 * and the tests use it to check that all 30 rounds can be won.
 */
#include "td.h"

typedef enum { PLACE, UPGRADE, TARGET } op;
typedef struct plan_step {
    op what;
    int tower;          /* PLACE: tower type; UPGRADE / TARGET: index in placement order */
    int arg;            /* UPGRADE: path; TARGET: targeting */
    float x, y;         /* PLACE: where */
} plan_step;

#define P(type, x, y)   { PLACE, type, 0, x, y }
#define U(tower, path)  { UPGRADE, tower, path, 0, 0 }
#define T(tower, prio)  { TARGET, tower, prio, 0, 0 }

/* Towers are numbered by placement order (0 = first placed). */
static const plan_step k_plan[] = {
    P(TD_DART, 100, 210),       /* 0: inside the left bend, sees two lanes   */
    P(TD_DART, 372, 108),       /* 1: inside the right bend                   */
    U(0, 1), U(1, 1), U(0, 0), U(1, 0),
    P(TD_TACK, 196, 110),       /* 2: between lanes 1 and 2                   */
    U(2, 1),
    P(TD_DART, 300, 214),       /* 3: between lanes 2 and 3                   */
    U(3, 1), U(3, 0),
    U(0, 1), U(1, 0),           /* eagle eye (sees camo), razor darts         */
    P(TD_BOMB, 236, 214),       /* 4 */
    U(4, 1), U(0, 0),
    P(TD_DART, 70, 116),        /* 5: where lanes 1 and 2 meet the edge       */
    U(5, 1), U(5, 0),
    P(TD_SNIPER, 456, 236),     /* 6 */
    U(6, 0),                    /* armor piercing: pops lead and black        */
    U(4, 0), U(2, 0),
    U(3, 1), U(3, 0), U(5, 1), U(1, 1),
    P(TD_BOMB, 160, 110),       /* 7 */
    U(7, 1), U(7, 0),
    U(2, 1), U(2, 1),           /* more tacks, ring of fire                   */
    U(4, 1), U(4, 1),           /* missiles, cluster bombs                    */
    U(6, 1), U(6, 0), T(6, TD_TARGET_STRONG),
    U(7, 1), U(7, 0),
    U(1, 1), U(0, 1),           /* triple shots                               */
    P(TD_SNIPER, 456, 270),     /* 8 */
    U(8, 0), U(8, 1), U(8, 0), T(8, TD_TARGET_STRONG),
    U(3, 0),                    /* spike ball                                 */
    U(6, 0),                    /* deadeye                                    */
    P(TD_ICE, 140, 210),        /* 9: freezes the left bend                   */
    U(9, 0), U(9, 1),
    P(TD_DART, 330, 216),       /* 10 */
    U(10, 0), U(10, 1), U(10, 0), U(10, 1), U(10, 0),
    U(8, 0), U(9, 1), U(6, 1),
};

void td_autopilot_init(td_autopilot *a) { a->next = 0; }

void td_autopilot_play(td_autopilot *a, td_game *g) {
    if (td_game_over(g)) return;
    while (a->next < (int)BR_ARRAY_COUNT(k_plan)) {
        const plan_step *s = &k_plan[a->next];
        td_result r = TD_OK;
        if (s->what == PLACE) r = td_place_tower(g, (td_tower_type)s->tower, br_v2(s->x, s->y));
        else if (s->what == UPGRADE) r = td_upgrade_tower(g, s->tower, s->arg);
        else if (s->tower < g->tower_count) g->towers[s->tower].targeting = (td_targeting)s->arg;
        if (r == TD_ERR_CASH) break;      /* save up for it */
        a->next++;                        /* done (or impossible: skip it) */
    }
    if (g->phase == TD_PHASE_BUILD) td_start_round(g);
}
