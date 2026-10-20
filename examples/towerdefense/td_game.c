/*
 * POP DEFENSE - the game: state, the per-step update and player commands.
 *
 * This is the file to read first: td_game_step() is the whole game loop,
 * and the commands below are everything a player can do.
 */
#include "td_internal.h"
#include <string.h>

void td_game_init(td_game *g) {
    memset(g, 0, sizeof(*g));
    td_map_build(&g->map);
    g->phase = TD_PHASE_BUILD;
    g->cash = TD_START_CASH;
    g->lives = TD_START_LIVES;
    g->next_id = 1;
}

uint32_t td_new_id(td_game *g) { return g->next_id++; }

td_event *td_emit(td_game *g, td_event_type type, br_vec2 pos) {
    static td_event overflow;           /* absorbs writes once the buffer is full */
    td_event *e;
    if (g->event_count < TD_MAX_EVENTS) e = &g->events[g->event_count++];
    else { e = &overflow; g->events_dropped++; }
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->pos = pos;
    return e;
}

void td_game_clear_events(td_game *g) { g->event_count = 0; }

bool td_game_over(const td_game *g) { return g->phase == TD_PHASE_DEFEAT || g->phase == TD_PHASE_VICTORY; }

static void finish_round(td_game *g) {
    int bonus = 100 + 5 * g->round, i;
    for (i = 0; i < g->tower_count; ++i) bonus += g->towers[i].stats.income;
    g->cash += bonus;
    td_emit(g, TD_EV_ROUND_END, br_v2(0, 0))->amount = bonus;
    if (g->round >= td_round_count()) {
        g->phase = TD_PHASE_VICTORY;
        td_emit(g, TD_EV_VICTORY, br_v2(0, 0));
    } else {
        g->phase = TD_PHASE_BUILD;
    }
}

/* ---- the game loop ---------------------------------------------------- */

void td_game_step(td_game *g) {
    if (td_game_over(g)) return;
    g->steps++;

    if (g->phase == TD_PHASE_ROUND) td_round_spawn(g, TD_STEP);   /* 1. new balloons enter    */
    td_balloons_move(g, TD_STEP);                                  /* 2. balloons move / leak  */
    td_towers_update(g, TD_STEP);                                  /* 3. towers attack         */
    td_projectiles_update(g, TD_STEP);                             /* 4. projectiles fly, pop  */
    td_balloons_remove_dead(g);                                    /* 5. tidy up               */
    td_projectiles_remove_dead(g);

    if (g->lives <= 0) {                                           /* 6. lost?                 */
        g->lives = 0;
        g->phase = TD_PHASE_DEFEAT;
        td_emit(g, TD_EV_DEFEAT, br_v2(0, 0));
    } else if (g->phase == TD_PHASE_ROUND && td_round_all_sent(g) && g->balloon_count == 0) {
        finish_round(g);                                           /* 7. round cleared?        */
    }
    if (td_game_over(g)) g->projectile_count = 0;                  /* nothing left in the air  */
}

/* ---- commands ------------------------------------------------------------ */

td_result td_start_round(td_game *g) {
    if (g->phase != TD_PHASE_BUILD) return TD_ERR_PHASE;
    td_round_begin(g);
    g->phase = TD_PHASE_ROUND;
    td_emit(g, TD_EV_ROUND_START, br_v2(0, 0))->amount = g->round;
    return TD_OK;
}

td_result td_can_place(const td_game *g, td_tower_type type, br_vec2 pos) {
    const td_tower_def *def = td_tower_info(type);
    int i;
    if (td_game_over(g)) return TD_ERR_PHASE;
    if (g->tower_count >= TD_MAX_TOWERS) return TD_ERR_FULL;
    if (!td_map_buildable(&g->map, pos, def->radius)) return TD_ERR_BLOCKED;
    for (i = 0; i < g->tower_count; ++i)
        if (br_v2_dist(pos, g->towers[i].pos) < def->radius + td_tower_info(g->towers[i].type)->radius)
            return TD_ERR_BLOCKED;
    if (g->cash < def->cost) return TD_ERR_CASH;
    return TD_OK;
}

td_result td_place_tower(td_game *g, td_tower_type type, br_vec2 pos) {
    const td_tower_def *def = td_tower_info(type);
    td_result r = td_can_place(g, type, pos);
    td_tower *t;
    if (r != TD_OK) return r;
    t = &g->towers[g->tower_count++];
    memset(t, 0, sizeof(*t));
    t->id = td_new_id(g);
    t->type = type;
    t->pos = pos;
    t->targeting = def->targeting;
    t->aim = br_v2(0, -1);
    t->spent = def->cost;
    td_tower_refresh_stats(t);
    g->cash -= def->cost;
    td_emit(g, TD_EV_TOWER_PLACED, pos)->kind = (int)type;
    return TD_OK;
}

/* Each path has 3 tiers; only one of the two paths may reach tier 3. */
td_result td_can_upgrade(const td_game *g, int tower, int path) {
    const td_tower *t;
    if (tower < 0 || tower >= g->tower_count || path < 0 || path >= TD_PATHS) return TD_ERR_NO_TOWER;
    if (td_game_over(g)) return TD_ERR_PHASE;
    t = &g->towers[tower];
    if (t->tier[path] >= TD_TIERS) return TD_ERR_MAXED;
    if (t->tier[path] == TD_TIERS - 1 && t->tier[1 - path] == TD_TIERS) return TD_ERR_PATH_LOCKED;
    if (g->cash < td_tower_info(t->type)->paths[path][t->tier[path]].cost) return TD_ERR_CASH;
    return TD_OK;
}

td_result td_upgrade_tower(td_game *g, int tower, int path) {
    td_result r = td_can_upgrade(g, tower, path);
    td_tower *t;
    td_event *ev;
    int cost;
    if (r != TD_OK) return r;
    t = &g->towers[tower];
    cost = td_tower_info(t->type)->paths[path][t->tier[path]].cost;
    g->cash -= cost;
    t->spent += cost;
    t->tier[path] += 1;
    td_tower_refresh_stats(t);
    ev = td_emit(g, TD_EV_TOWER_UPGRADED, t->pos);
    ev->kind = (int)t->type;
    ev->amount = path;
    return TD_OK;
}

int td_sell_value(const td_tower *t) { return t->spent * TD_SELL_PERCENT / 100; }

int td_sell_tower(td_game *g, int tower) {
    int refund;
    if (tower < 0 || tower >= g->tower_count || td_game_over(g)) return 0;
    refund = td_sell_value(&g->towers[tower]);
    g->cash += refund;
    td_emit(g, TD_EV_TOWER_SOLD, g->towers[tower].pos)->amount = refund;
    memmove(&g->towers[tower], &g->towers[tower + 1], (size_t)(g->tower_count - tower - 1) * sizeof(td_tower));
    g->tower_count -= 1;
    return refund;
}

void td_cycle_targeting(td_game *g, int tower, int dir) {
    td_tower *t;
    if (tower < 0 || tower >= g->tower_count) return;
    t = &g->towers[tower];
    t->targeting = (td_targeting)(((int)t->targeting + dir % TD_TARGETING_COUNT + TD_TARGETING_COUNT) % TD_TARGETING_COUNT);
}

int td_tower_at(const td_game *g, br_vec2 p) {
    int i;
    for (i = g->tower_count - 1; i >= 0; --i)
        if (br_v2_dist(p, g->towers[i].pos) <= td_tower_info(g->towers[i].type)->radius + 2.0f) return i;
    return -1;
}

const char *td_result_text(td_result r) {
    switch (r) {
    case TD_OK:              return "OK";
    case TD_ERR_CASH:        return "Not enough cash";
    case TD_ERR_BLOCKED:     return "Can't build there";
    case TD_ERR_FULL:        return "Too many towers";
    case TD_ERR_MAXED:       return "Fully upgraded";
    case TD_ERR_PATH_LOCKED: return "Other path is at tier 3";
    case TD_ERR_NO_TOWER:    return "No tower";
    case TD_ERR_PHASE:       return "Not now";
    }
    return "?";
}
