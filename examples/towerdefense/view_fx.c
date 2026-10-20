/*
 * POP DEFENSE - visual effects as ECS entities.
 *
 * Each rule event (td_event) becomes a few short-lived entities: particles,
 * expanding rings, floating text and sniper tracers. Two systems run them:
 * one moves and ages them, the other draws them. When an entity's life
 * runs out it is destroyed. The rules never know any of this exists.
 */
#include "view.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_FX_ENTITIES 3000

typedef struct fx_particle { br_vec2 pos, vel; float life, max_life, size, gravity; br_color color; } fx_particle;
typedef struct fx_ring     { br_vec2 pos; float from, to, life, max_life; br_color color; bool filled; } fx_ring;
typedef struct fx_text     { br_vec2 pos; float life, max_life, scale; br_color color; char text[20]; } fx_text;
typedef struct fx_tracer   { br_vec2 from, to; float life; } fx_tracer;

static const uint32_t k_pop_colors[TD_BALLOON_TYPE_COUNT] = {
    0xE0302EFF, 0x2F86E8FF, 0x3CBF3CFF, 0xF8D73AFF, 0xF77FB5FF, 0x303038FF,
    0xF2F2F2FF, 0x9CA2AEFF, 0xD8D8D8FF, 0xF5922AFF, 0xB0703CFF, 0x5E86B8FF,
};

/* ---- spawning ------------------------------------------------------------------ */

static bool room(const view_fx *fx) { return br_world_entity_count(fx->world) < MAX_FX_ENTITIES; }

static void particles(view_fx *fx, br_vec2 at, int count, br_color color, float speed, float life, float size, float gravity) {
    int i;
    for (i = 0; i < count && room(fx); ++i) {
        fx_particle p;
        float a = br_rng_range(&fx->rng, 0, BR_TAU), v = br_rng_range(&fx->rng, speed * 0.4f, speed);
        p.pos = at;
        p.vel = br_v2(cosf(a) * v, sinf(a) * v);
        p.max_life = p.life = br_rng_range(&fx->rng, life * 0.6f, life);
        p.size = size;
        p.gravity = gravity;
        p.color = br_color_lerp(color, BR_WHITE, br_rng_range(&fx->rng, 0, 0.3f));
        br_add(fx->world, br_entity_create(fx->world), fx->particle, &p);
    }
}

static void ring(view_fx *fx, br_vec2 at, float from, float to, float life, br_color color, bool filled) {
    fx_ring r;
    if (!room(fx)) return;
    r.pos = at; r.from = from; r.to = to;
    r.life = r.max_life = life;
    r.color = color;
    r.filled = filled;
    br_add(fx->world, br_entity_create(fx->world), fx->ring, &r);
}

void view_fx_text(view_fx *fx, br_vec2 pos, const char *text, br_color color, float scale, float life) {
    fx_text t;
    memset(&t, 0, sizeof(t));
    t.pos = pos;
    t.life = t.max_life = life;
    t.scale = scale;
    t.color = color;
    snprintf(t.text, sizeof(t.text), "%s", text);
    br_add(fx->world, br_entity_create(fx->world), fx->text, &t);
}

void view_fx_clear(view_fx *fx) { br_world_clear(fx->world); }

/* ---- rule events -> effects ----------------------------------------------------------- */

void view_fx_event(view_fx *fx, const td_event *e) {
    char buf[24];
    br_vec2 center = br_v2(TD_MAP_W / 2.0f, TD_MAP_H / 2.0f - 30);
    switch (e->type) {
    case TD_EV_POP: {
        br_color c = br_hex(k_pop_colors[e->kind]);
        if (e->kind == TD_BLIMP) {
            particles(fx, e->pos, 60, c, 160, 0.8f, 2.5f, 60);
            ring(fx, e->pos, 6, 46, 0.35f, br_rgba(255, 255, 255, 220), false);
        } else {
            particles(fx, e->pos, 5, c, 90, 0.35f, 1.5f, 0);
            ring(fx, e->pos, 2, e->radius + 5, 0.12f, br_rgba(255, 255, 255, 230), false);
        }
        break;
    }
    case TD_EV_HIT:
        particles(fx, e->pos, 2, e->kind == TD_CERAMIC ? br_rgb(150, 95, 50) : br_rgb(90, 110, 140), 60, 0.25f, 1.0f, 120);
        break;
    case TD_EV_BLOCKED:
        particles(fx, e->pos, 2, BR_WHITE, 70, 0.15f, 1.0f, 0);
        break;
    case TD_EV_LEAK: {
        br_vec2 at = br_v2(br_clampf(e->pos.x, 20, TD_MAP_W - 20), br_clampf(e->pos.y, 20, TD_MAP_H - 24));
        snprintf(buf, sizeof(buf), "-%d", e->amount);
        view_fx_text(fx, at, buf, br_rgb(255, 80, 80), 2, 1.0f);
        ring(fx, at, 4, 30, 0.4f, br_rgba(255, 60, 60, 200), false);
        break;
    }
    case TD_EV_ATTACK:
        if (e->kind == TD_BOMB) particles(fx, br_v2_add(e->pos, br_v2_scale(e->to, 12)), 3, br_rgb(170, 170, 170), 25, 0.3f, 1.5f, -20);
        break;
    case TD_EV_EXPLOSION:
        ring(fx, e->pos, e->radius * 0.3f, e->radius, 0.22f, br_rgba(255, 170, 40, 170), true);
        ring(fx, e->pos, e->radius * 0.5f, e->radius + 3, 0.18f, br_rgba(255, 240, 150, 230), false);
        particles(fx, e->pos, 5, br_rgb(120, 110, 100), 30, 0.6f, 2.0f, -25);
        break;
    case TD_EV_PULSE:
        if (e->kind == TD_ICE) {
            ring(fx, e->pos, 4, e->radius, 0.35f, br_rgba(170, 230, 255, 110), true);
            ring(fx, e->pos, 4, e->radius, 0.35f, br_rgba(240, 252, 255, 220), false);
        } else {
            ring(fx, e->pos, 8, e->radius, 0.25f, br_rgba(255, 120, 30, 120), true);
            ring(fx, e->pos, 8, e->radius, 0.25f, br_rgba(255, 220, 90, 230), false);
            particles(fx, e->pos, 10, br_rgb(255, 150, 40), e->radius * 3.0f, 0.3f, 2.0f, -60);
        }
        break;
    case TD_EV_SNIPE: {
        fx_tracer t;
        t.from = e->pos; t.to = e->to; t.life = 0.12f;
        if (room(fx)) br_add(fx->world, br_entity_create(fx->world), fx->tracer, &t);
        particles(fx, e->pos, 3, br_rgb(255, 230, 120), 40, 0.12f, 1.5f, 0);
        break;
    }
    case TD_EV_ROUND_START:
        snprintf(buf, sizeof(buf), "ROUND %d", e->amount);
        view_fx_text(fx, center, buf, BR_WHITE, 3, 1.6f);
        break;
    case TD_EV_ROUND_END:
        snprintf(buf, sizeof(buf), "ROUND CLEAR  +$%d", e->amount);
        view_fx_text(fx, center, buf, br_rgb(255, 225, 100), 2, 2.0f);
        break;
    case TD_EV_TOWER_PLACED:
        ring(fx, e->pos, 6, 20, 0.3f, br_rgba(230, 210, 170, 200), false);
        particles(fx, e->pos, 10, br_rgb(190, 160, 110), 50, 0.4f, 1.5f, 0);
        break;
    case TD_EV_TOWER_UPGRADED:
        ring(fx, e->pos, 4, 24, 0.4f, e->amount == 0 ? br_rgba(255, 170, 40, 230) : br_rgba(80, 190, 255, 230), false);
        particles(fx, e->pos, 14, br_rgb(255, 230, 120), 70, 0.5f, 1.5f, -30);
        break;
    case TD_EV_TOWER_SOLD:
        snprintf(buf, sizeof(buf), "+$%d", e->amount);
        view_fx_text(fx, e->pos, buf, br_rgb(255, 225, 100), 1, 1.0f);
        particles(fx, e->pos, 12, br_rgb(250, 210, 70), 80, 0.5f, 2.0f, 200);
        break;
    default:
        break;
    }
}

/* ---- systems -------------------------------------------------------------------------- */

static void sys_fx_update(br_world *w, float dt, void *user) {
    view_fx *fx = (view_fx *)user;
    br_query q = br_query_make(w, &fx->particle, 1);
    while (br_query_next(&q)) {
        fx_particle *p = BR_QUERY_GET(&q, 0, fx_particle);
        p->vel.y += p->gravity * dt;
        p->vel = br_v2_scale(p->vel, 1.0f - 2.5f * dt);      /* air drag */
        p->pos = br_v2_add(p->pos, br_v2_scale(p->vel, dt));
        if ((p->life -= dt) <= 0) br_entity_destroy(w, q.entity);
    }
    q = br_query_make(w, &fx->ring, 1);
    while (br_query_next(&q))
        if ((BR_QUERY_GET(&q, 0, fx_ring)->life -= dt) <= 0) br_entity_destroy(w, q.entity);
    q = br_query_make(w, &fx->text, 1);
    while (br_query_next(&q)) {
        fx_text *t = BR_QUERY_GET(&q, 0, fx_text);
        t->pos.y -= (t->scale > 1 ? 8.0f : 20.0f) * dt;
        if ((t->life -= dt) <= 0) br_entity_destroy(w, q.entity);
    }
    q = br_query_make(w, &fx->tracer, 1);
    while (br_query_next(&q))
        if ((BR_QUERY_GET(&q, 0, fx_tracer)->life -= dt) <= 0) br_entity_destroy(w, q.entity);
}

static br_color fade(br_color c, float k) { c.a = (uint8_t)((float)c.a * br_clampf(k, 0, 1)); return c; }

static void sys_fx_draw(br_world *w, float dt, void *user) {
    view_fx *fx = (view_fx *)user;
    br_renderer *r = &BR_WORLD_ENGINE(w)->renderer;
    br_query q;
    BR_UNUSED(dt);
    br_set_clip(r, br_recti_make(0, 0, TD_MAP_W, TD_MAP_H));     /* effects stay on the map */

    q = br_query_make(w, &fx->ring, 1);
    while (br_query_next(&q)) {
        fx_ring *g = BR_QUERY_GET(&q, 0, fx_ring);
        float k = g->life / g->max_life, rad = br_lerpf(g->to, g->from, k);
        if (g->filled) br_fill_circle(r, g->pos, rad, fade(g->color, k));
        else br_draw_circle(r, g->pos, rad, fade(g->color, k));
    }
    q = br_query_make(w, &fx->tracer, 1);
    while (br_query_next(&q)) {
        fx_tracer *t = BR_QUERY_GET(&q, 0, fx_tracer);
        br_draw_line(r, t->from, t->to, br_rgba(255, 240, 160, (uint8_t)(255.0f * br_clampf(t->life / 0.12f, 0, 1))));
    }
    q = br_query_make(w, &fx->particle, 1);
    while (br_query_next(&q)) {
        fx_particle *p = BR_QUERY_GET(&q, 0, fx_particle);
        float k = p->life / p->max_life;
        br_fill_rect(r, br_rect_make(p->pos.x - p->size / 2, p->pos.y - p->size / 2, p->size, p->size), fade(p->color, k * 2.0f));
    }
    br_reset_clip(r);
    q = br_query_make(w, &fx->text, 1);
    while (br_query_next(&q)) {
        fx_text *t = BR_QUERY_GET(&q, 0, fx_text);
        view_draw_text_centered(r, t->text, t->pos.x, t->pos.y, t->scale, fade(t->color, t->life / 0.4f));
    }
}

void view_fx_init(view_fx *fx, br_world *world) {
    fx->world = world;
    fx->particle = BR_COMPONENT(world, fx_particle);
    fx->ring = BR_COMPONENT(world, fx_ring);
    fx->text = BR_COMPONENT(world, fx_text);
    fx->tracer = BR_COMPONENT(world, fx_tracer);
    br_rng_seed(&fx->rng, 77);
    br_system_add(world, "fx_update", BR_PHASE_UPDATE, sys_fx_update, fx, 0);
    br_system_add(world, "fx_draw", BR_PHASE_RENDER, sys_fx_draw, fx, 10);
}
