/*
 * POP DEFENSE - towers: choosing a target and attacking it.
 */
#include "td_internal.h"

/* Unit-vector rotations, as constants so the rules never call sin/cos
 * (keeps results bit-identical on every CPU and C library). */
typedef struct rot { float c, s; } rot;
static const rot k_rot15 = { 0.96592583f, 0.25881905f };   /* triple-shot spread */

static br_vec2 rotate(br_vec2 v, rot r) { return br_v2(v.x * r.c - v.y * r.s, v.x * r.s + v.y * r.c); }

static rot radial_step(int count) {      /* 360 degrees / count */
    rot r;
    switch (count) {
    case 12: r.c = 0.86602540f; r.s = 0.5f; break;
    case 16: r.c = 0.92387953f; r.s = 0.38268343f; break;
    default: r.c = 0.70710678f; r.s = 0.70710678f; break;   /* 8 */
    }
    return r;
}

void td_tower_refresh_stats(td_tower *t) {
    const td_tower_def *def = td_tower_info(t->type);
    int path, tier;
    t->stats = def->base;
    for (path = 0; path < TD_PATHS; ++path)
        for (tier = 0; tier < t->tier[path]; ++tier)
            def->paths[path][tier].apply(&t->stats);
}

void td_tower_credit(td_game *g, uint32_t tower, int pops) {
    int i;
    for (i = 0; i < g->tower_count; ++i)
        if (g->towers[i].id == tower) { g->towers[i].pops += pops; return; }
}

/* ---- targeting ---------------------------------------------------------- */

static bool in_range(const td_tower *t, const td_balloon *b) {
    return br_v2_dist(t->pos, b->pos) <= t->stats.range + td_balloon_info(b->type)->radius;
}

static bool can_target(const td_tower *t, const td_balloon *b) {
    if (!b->alive || (b->camo && !t->stats.camo)) return false;
    if (t->stats.attack == TD_ATK_HITSCAN)   /* anywhere, once it's on screen */
        return b->pos.x >= 0 && b->pos.y >= 0 && b->pos.x < TD_MAP_W && b->pos.y < TD_MAP_H;
    return in_range(t, b);
}

/* The best balloon to shoot by the tower's priority, or -1. */
static int pick_target(const td_game *g, const td_tower *t) {
    int i, best = -1;
    float best_score = 0;
    for (i = 0; i < g->balloon_count; ++i) {
        const td_balloon *b = &g->balloons[i];
        float score;
        if (!can_target(t, b)) continue;
        switch (t->targeting) {
        case TD_TARGET_LAST:   score = -b->dist; break;
        case TD_TARGET_STRONG: score = (float)b->type * 100000.0f + b->dist; break;   /* types are ordered by strength */
        case TD_TARGET_CLOSE:  score = -br_v2_dist(t->pos, b->pos); break;
        default:               score = b->dist; break;                               /* first */
        }
        if (best < 0 || score > best_score) { best = i; best_score = score; }
    }
    return best;
}

/* Where to throw so the projectile meets the balloon. */
static br_vec2 lead_target(const td_game *g, const td_tower *t, const td_balloon *b) {
    float flight = br_v2_dist(t->pos, b->pos) / t->stats.speed;
    return td_map_point(&g->map, b->dist + td_balloon_speed(b) * flight);
}

/* ---- attacks -------------------------------------------------------------- */

static void attack_aimed(td_game *g, td_tower *t, const td_balloon *target) {
    br_vec2 aim = br_v2_norm(br_v2_sub(lead_target(g, t, target), t->pos));
    td_event *ev;
    if (aim.x == 0 && aim.y == 0) aim = br_v2(0, -1);
    t->aim = aim;
    td_projectile_spawn(g, t, t->pos, aim);
    if (t->stats.count >= 3) {
        rot back = { k_rot15.c, -k_rot15.s };
        td_projectile_spawn(g, t, t->pos, rotate(aim, k_rot15));
        td_projectile_spawn(g, t, t->pos, rotate(aim, back));
    }
    ev = td_emit(g, TD_EV_ATTACK, t->pos);
    ev->kind = (int)t->type;
    ev->to = aim;
}

static void attack_radial(td_game *g, td_tower *t) {
    rot step = radial_step(t->stats.count);
    br_vec2 dir = br_v2(1, 0);
    int i;
    for (i = 0; i < t->stats.count; ++i) {
        td_projectile_spawn(g, t, t->pos, dir);
        dir = rotate(dir, step);
    }
    td_emit(g, TD_EV_ATTACK, t->pos)->kind = (int)t->type;
}

/* Ice freeze or ring of fire: everything in range (camo included). */
static void attack_pulse(td_game *g, td_tower *t) {
    const td_stats *s = &t->stats;
    int i, n = g->balloon_count, hits = 0;
    td_event *ev;
    for (i = 0; i < n && hits < s->pierce; ++i) {
        td_balloon *b = &g->balloons[i];
        if (!b->alive || !in_range(t, b) || !td_balloon_hurt_by(b, s->damage_type)) continue;
        ++hits;
        if (s->freeze > 0) td_balloon_freeze(b, s);
        if (s->damage > 0) td_balloon_damage(g, i, s->damage, s->damage_type, 0, t->id);
    }
    ev = td_emit(g, TD_EV_PULSE, t->pos);
    ev->kind = (int)t->type;
    ev->radius = s->range;
}

static void attack_hitscan(td_game *g, td_tower *t, int target) {
    br_vec2 at = g->balloons[target].pos;
    td_event *ev = td_emit(g, TD_EV_SNIPE, t->pos);
    ev->to = at;
    t->aim = br_v2_norm(br_v2_sub(at, t->pos));
    td_balloon_damage(g, target, t->stats.damage, t->stats.damage_type, 0, t->id);
}

/* Arctic wind: everything near the tower moves at half speed. */
static void apply_aura(td_game *g, const td_tower *t) {
    int i;
    for (i = 0; i < g->balloon_count; ++i) {
        td_balloon *b = &g->balloons[i];
        if (!b->alive || !in_range(t, b) || !td_balloon_hurt_by(b, TD_DMG_COLD)) continue;
        if (b->slowed < 0.05f) b->slowed = 0.05f;
        if (b->slow_factor > 0.5f) b->slow_factor = 0.5f;
    }
}

void td_towers_update(td_game *g, float dt) {
    int i;
    for (i = 0; i < g->tower_count; ++i) {
        td_tower *t = &g->towers[i];
        int target;
        if (t->stats.aura) apply_aura(g, t);
        if (t->cooldown > 0) t->cooldown -= dt;
        if (t->cooldown > 0) continue;

        target = pick_target(g, t);
        if (target < 0) continue;            /* nothing to shoot: stay ready */
        switch (t->stats.attack) {
        case TD_ATK_AIMED:   attack_aimed(g, t, &g->balloons[target]); break;
        case TD_ATK_RADIAL:  attack_radial(g, t); break;
        case TD_ATK_PULSE:   attack_pulse(g, t); break;
        case TD_ATK_HITSCAN: attack_hitscan(g, t, target); break;
        }
        t->cooldown += t->stats.interval;
    }
}
