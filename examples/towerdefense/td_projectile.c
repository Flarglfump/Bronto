/*
 * POP DEFENSE - projectiles: darts, tacks and bombs in flight.
 *
 * A projectile flies in a straight line until it has hit `pierce`
 * balloons, bounces off an immune one, or runs out of range. Bombs explode
 * on the first balloon they touch.
 */
#include "td_internal.h"
#include <string.h>

static td_projectile *alloc_projectile(td_game *g) {
    td_projectile *p;
    if (g->projectile_count >= TD_MAX_PROJECTILES) return NULL;
    p = &g->projectiles[g->projectile_count++];
    memset(p, 0, sizeof(*p));
    p->id = td_new_id(g);
    p->alive = true;
    return p;
}

td_projectile *td_projectile_spawn(td_game *g, const td_tower *t, br_vec2 pos, br_vec2 dir) {
    const td_stats *s = &t->stats;
    td_projectile *p = alloc_projectile(g);
    if (!p) return NULL;
    p->tower = t->id;
    p->kind = s->proj;
    p->pos = pos;
    p->vel = br_v2_scale(dir, s->speed);
    p->radius = s->proj_radius;
    p->life = (s->range + 24.0f) / s->speed;    /* a little past the tower's range */
    p->damage = s->damage;
    p->pierce = s->pierce;
    p->damage_type = s->damage_type;
    p->splash = s->splash;
    p->splash_pierce = s->splash_pierce;
    p->cluster = s->cluster;
    p->stun = s->stun;
    return p;
}

static bool already_hit(const td_projectile *p, uint32_t balloon) {
    int i;
    for (i = 0; i < p->hit_count; ++i)
        if (p->hits[i] == balloon) return true;
    return false;
}

/* Cluster bombs: small bombs scattered in 8 directions that go off shortly. */
static void scatter_bomblets(td_game *g, const td_projectile *bomb) {
    static const br_vec2 dirs[8] = { { 1, 0 }, { 0.70710678f, 0.70710678f }, { 0, 1 }, { -0.70710678f, 0.70710678f },
                                     { -1, 0 }, { -0.70710678f, -0.70710678f }, { 0, -1 }, { 0.70710678f, -0.70710678f } };
    int i;
    for (i = 0; i < bomb->cluster && i < 8; ++i) {
        td_projectile *p = alloc_projectile(g);
        if (!p) return;
        p->tower = bomb->tower;
        p->kind = TD_PROJ_BOMBLET;
        p->pos = bomb->pos;
        p->vel = br_v2_scale(dirs[i], 150.0f);
        p->radius = 2.5f;
        p->life = 0.28f;
        p->damage = bomb->damage;
        p->pierce = 1;
        p->damage_type = bomb->damage_type;
        p->splash = 14.0f;
        p->splash_pierce = 6;
        p->stun = bomb->stun;
        p->explode_on_expire = true;
    }
}

static void explode(td_game *g, td_projectile *p) {
    int i, n = g->balloon_count, hits = 0;
    td_event *ev = td_emit(g, TD_EV_EXPLOSION, p->pos);
    ev->radius = p->splash;
    ev->kind = (int)p->kind;
    p->alive = false;
    for (i = 0; i < n && hits < p->splash_pierce; ++i) {   /* children released now aren't hit twice */
        td_balloon *b = &g->balloons[i];
        if (!b->alive || br_v2_dist(b->pos, p->pos) > p->splash + td_balloon_info(b->type)->radius) continue;
        ++hits;
        if (p->stun > 0 && b->type != TD_BLIMP && td_balloon_hurt_by(b, p->damage_type) && b->stunned < p->stun)
            b->stunned = p->stun;
        td_balloon_damage(g, i, p->damage, p->damage_type, p->id, p->tower);
    }
    if (p->cluster > 0) scatter_bomblets(g, p);
}

static bool off_map(br_vec2 p) {
    return p.x < -40 || p.y < -40 || p.x > TD_MAP_W + 40 || p.y > TD_MAP_H + 40;
}

void td_projectiles_update(td_game *g, float dt) {
    int i, j, n = g->projectile_count;
    for (i = 0; i < n; ++i) {
        td_projectile *p = &g->projectiles[i];
        int balloons = g->balloon_count;
        if (!p->alive) continue;
        p->pos = br_v2_add(p->pos, br_v2_scale(p->vel, dt));
        p->life -= dt;
        if (p->life <= 0 || off_map(p->pos)) {
            if (p->explode_on_expire) explode(g, p);
            p->alive = false;
            continue;
        }
        for (j = 0; j < balloons && p->alive; ++j) {
            td_balloon *b = &g->balloons[j];
            int result;
            if (!b->alive || b->shield == p->id || already_hit(p, b->id)) continue;
            if (br_v2_dist(p->pos, b->pos) > p->radius + td_balloon_info(b->type)->radius) continue;
            if (p->splash > 0) { explode(g, p); break; }
            if (p->hit_count < TD_MAX_HITS) p->hits[p->hit_count++] = b->id;
            result = td_balloon_damage(g, j, p->damage, p->damage_type, p->id, p->tower);
            if (result == TD_BLOCKED || --p->pierce <= 0) p->alive = false;   /* lead: bounces off */
        }
    }
}

void td_projectiles_remove_dead(td_game *g) {
    int i, n = 0;
    for (i = 0; i < g->projectile_count; ++i)
        if (g->projectiles[i].alive) g->projectiles[n++] = g->projectiles[i];
    g->projectile_count = n;
}
