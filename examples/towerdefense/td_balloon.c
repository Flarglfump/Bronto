/*
 * POP DEFENSE - balloons: spawning, moving along the path, popping, leaking.
 */
#include "td_internal.h"
#include <string.h>

td_balloon *td_balloon_spawn(td_game *g, td_balloon_type type, bool camo, float dist) {
    td_balloon *b;
    if (g->balloon_count >= TD_MAX_BALLOONS) return NULL;
    b = &g->balloons[g->balloon_count++];
    memset(b, 0, sizeof(*b));
    b->id = td_new_id(g);
    b->type = type;
    b->camo = camo;
    b->alive = true;
    b->hp = td_balloon_info(type)->hp;
    b->dist = dist;
    b->slow_factor = 1.0f;
    b->pos = td_map_point(&g->map, dist);
    return b;
}

bool td_balloon_hurt_by(const td_balloon *b, td_damage type) {
    return (td_balloon_info(b->type)->immune & TD_IMMUNE(type)) == 0;
}

/* Lives lost if this balloon gets through: its remaining layers. */
int td_balloon_rbe(const td_balloon *b) {
    return td_balloon_type_rbe(b->type) - td_balloon_info(b->type)->hp + b->hp;
}

float td_balloon_speed(const td_balloon *b) {
    float speed = td_balloon_info(b->type)->speed;
    if (b->frozen > 0 || b->stunned > 0) return 0.0f;
    if (b->slowed > 0) speed *= b->slow_factor;
    return speed;
}

void td_balloon_freeze(td_balloon *b, const td_stats *s) {
    if (b->frost_guard > 0) return;       /* just thawed: give it a moment */
    b->frozen = s->freeze;
    b->frost_guard = s->freeze + 0.6f;
    if (s->slow > 0) {                    /* permafrost: slow once thawed */
        b->slowed = s->slow;
        b->slow_factor = s->slow_factor;
    }
}

int td_balloon_damage(td_game *g, int index, int damage, td_damage type, uint32_t projectile, uint32_t tower) {
    td_balloon *b = &g->balloons[index];
    const td_balloon_def *def = td_balloon_info(b->type);
    td_event *ev;
    int pops = 1, leftover, i;

    if (!b->alive || damage <= 0) return 0;
    if (!td_balloon_hurt_by(b, type)) {
        td_emit(g, TD_EV_BLOCKED, b->pos)->kind = (int)b->type;
        return TD_BLOCKED;
    }
    b->hp -= damage;
    if (b->hp > 0) {                      /* ceramic / blimp: took a hit, still whole */
        ev = td_emit(g, TD_EV_HIT, b->pos);
        ev->kind = (int)b->type;
        ev->amount = b->hp;
        return 0;
    }

    /* Pop! $1 per layer, and the children take its place on the path. */
    leftover = -b->hp;
    b->alive = false;
    g->cash += 1;
    g->pops += 1;
    td_tower_credit(g, tower, 1);
    ev = td_emit(g, TD_EV_POP, b->pos);
    ev->kind = (int)b->type;
    ev->radius = def->radius;

    for (i = 0; i < def->child_count; ++i) {
        float spread = ((float)i - (float)(def->child_count - 1) * 0.5f) * 7.0f;
        float at = b->dist + spread > 0 ? b->dist + spread : 0.0f;
        td_balloon *child = td_balloon_spawn(g, def->children[i], b->camo, at);
        int child_pops;
        if (!child) break;
        child->shield = projectile;       /* the same dart doesn't hit it again */
        child->stunned = b->stunned;
        if (leftover > 0) {               /* extra damage carries through */
            child_pops = td_balloon_damage(g, (int)(child - g->balloons), leftover, type, projectile, tower);
            if (child_pops > 0) pops += child_pops;
        }
    }
    return pops;
}

void td_balloons_move(td_game *g, float dt) {
    int i;
    for (i = 0; i < g->balloon_count; ++i) {
        td_balloon *b = &g->balloons[i];
        if (!b->alive) continue;
        b->dist += td_balloon_speed(b) * dt;

        if (b->frozen > 0) b->frozen -= dt;
        else if (b->slowed > 0) b->slowed -= dt;     /* slow wears off once thawed */
        if (b->slowed <= 0) b->slow_factor = 1.0f;
        if (b->stunned > 0) b->stunned -= dt;
        if (b->frost_guard > 0) b->frost_guard -= dt;

        if (b->dist >= g->map.length) {               /* got through: lose lives */
            int cost = td_balloon_rbe(b);
            td_event *ev = td_emit(g, TD_EV_LEAK, g->map.points[g->map.point_count - 1]);
            ev->kind = (int)b->type;
            ev->amount = cost;
            g->lives -= cost;
            g->leaked += cost;
            b->alive = false;
            continue;
        }
        b->pos = td_map_point(&g->map, b->dist);
    }
}

/* Drop popped and leaked balloons, keeping the rest in order. */
void td_balloons_remove_dead(td_game *g) {
    int i, n = 0;
    for (i = 0; i < g->balloon_count; ++i)
        if (g->balloons[i].alive) g->balloons[n++] = g->balloons[i];
    g->balloon_count = n;
}
