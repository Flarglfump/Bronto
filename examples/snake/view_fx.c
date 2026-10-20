/*
 * SNAKE - visual effects as ECS entities.
 *
 * Each rule event (snake_event) becomes a few short-lived entities:
 * particles and floating score pop-ups. One system moves and ages them,
 * another draws them. When an entity's life runs out it is destroyed.
 * The rules never know any of this exists.
 */
#include "view.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct fx_particle { br_vec2 pos, vel; float life, max_life, size; br_color color; } fx_particle;
typedef struct fx_popup    { br_vec2 pos; float life; char text[12]; br_color color; } fx_popup;

/* ---- spawning ------------------------------------------------------------- */

static void burst(view_fx *fx, br_vec2 at, int count, br_color base, float speed) {
    int i;
    for (i = 0; i < count; ++i) {
        br_entity e = br_entity_create(fx->world);
        fx_particle p;
        float a = br_rng_range(&fx->rng, 0, BR_TAU), v = br_rng_range(&fx->rng, speed * 0.3f, speed);
        p.pos = at;
        p.vel = br_v2(cosf(a) * v, sinf(a) * v - speed * 0.4f);   /* a little upward kick */
        p.max_life = p.life = br_rng_range(&fx->rng, 0.35f, 0.8f);
        p.size = br_rng_range(&fx->rng, 1.0f, 2.5f);
        p.color = br_color_lerp(base, BR_WHITE, br_rng_range(&fx->rng, 0, 0.4f));
        br_add(fx->world, e, fx->particle, &p);
    }
}

static void popup(view_fx *fx, br_vec2 at, const char *text, br_color color) {
    br_entity e = br_entity_create(fx->world);
    fx_popup p;
    memset(&p, 0, sizeof(p));
    p.pos = at;
    p.life = 0.9f;
    p.color = color;
    snprintf(p.text, sizeof(p.text), "%s", text);
    br_add(fx->world, e, fx->popup, &p);
}

/* ---- rule events -> effects ----------------------------------------------- */

void view_fx_event(view_fx *fx, const snake_event *e, const snake_game *g) {
    if (e->type == SNAKE_EV_ATE || e->type == SNAKE_EV_ATE_BONUS) {
        bool bonus = e->type == SNAKE_EV_ATE_BONUS;
        br_vec2 c = view_cell_center(e->cell);
        char text[12];
        burst(fx, c, bonus ? 40 : 14, bonus ? br_rgb(255, 210, 60) : br_rgb(230, 70, 70), bonus ? 110.0f : 70.0f);
        if (bonus) snprintf(text, sizeof(text), "BONUS!");
        else snprintf(text, sizeof(text), "+%d", e->points);
        popup(fx, br_v2(c.x, c.y - 8), text, bonus ? BR_YELLOW : BR_WHITE);
        if (bonus) fx->shake = 0.15f;
    } else if (e->type == SNAKE_EV_DIED || e->type == SNAKE_EV_WON) {
        bool won = e->type == SNAKE_EV_WON;
        int i;
        for (i = 0; i < g->body.length; i += 2)            /* the body bursts apart */
            burst(fx, view_cell_center(snake_segment(g, i)), 3, won ? BR_YELLOW : br_rgb(120, 220, 100), 90.0f);
        fx->shake = won ? 0.2f : 0.35f;
    }
}

br_camera2d view_fx_camera(view_fx *fx) {
    br_camera2d cam = br_camera2d_make(br_v2(0, 0), br_v2(0, 0));
    if (fx->shake > 0) {
        float m = fx->shake * 10.0f;
        cam.offset.y = br_rng_range(&fx->rng, -m, m);
        cam.offset.x = br_rng_range(&fx->rng, -m, m);
    }
    return cam;
}

/* ---- systems ---------------------------------------------------------------- */

static void sys_fx_update(br_world *w, float dt, void *user) {
    view_fx *fx = (view_fx *)user;
    br_query q = br_query_make(w, &fx->particle, 1);
    if (fx->shake > 0) fx->shake -= dt;
    while (br_query_next(&q)) {
        fx_particle *p = BR_QUERY_GET(&q, 0, fx_particle);
        p->vel.y += 260.0f * dt;                                 /* gravity */
        p->pos = br_v2_add(p->pos, br_v2_scale(p->vel, dt));
        p->life -= dt;
        if (p->life <= 0) br_entity_destroy(w, q.entity);       /* safe while iterating */
    }
    q = br_query_make(w, &fx->popup, 1);
    while (br_query_next(&q)) {
        fx_popup *p = BR_QUERY_GET(&q, 0, fx_popup);
        p->pos.y -= 22.0f * dt;                                  /* float up */
        p->life -= dt;
        if (p->life <= 0) br_entity_destroy(w, q.entity);
    }
}

static void sys_fx_draw(br_world *w, float dt, void *user) {
    view_fx *fx = (view_fx *)user;
    br_renderer *r = &BR_WORLD_ENGINE(w)->renderer;
    br_query q = br_query_make(w, &fx->particle, 1);
    BR_UNUSED(dt);
    while (br_query_next(&q)) {
        fx_particle *p = BR_QUERY_GET(&q, 0, fx_particle);
        br_color c = p->color;
        c.a = (uint8_t)(255.0f * br_clampf(p->life / p->max_life, 0, 1));
        br_fill_rect(r, br_rect_make(p->pos.x, p->pos.y, p->size, p->size), c);
    }
    q = br_query_make(w, &fx->popup, 1);
    while (br_query_next(&q)) {
        fx_popup *p = BR_QUERY_GET(&q, 0, fx_popup);
        br_color c = p->color;
        c.a = (uint8_t)(255.0f * br_clampf(p->life / 0.4f, 0, 1));
        br_draw_text(r, p->text, p->pos.x - br_measure_text(p->text, 1).x / 2, p->pos.y, 1, c);
    }
}

void view_fx_init(view_fx *fx, br_world *world, uint64_t seed) {
    fx->world = world;
    fx->particle = BR_COMPONENT(world, fx_particle);
    fx->popup = BR_COMPONENT(world, fx_popup);
    fx->shake = 0;
    br_rng_seed(&fx->rng, seed);
    br_system_add(world, "fx_update", BR_PHASE_UPDATE, sys_fx_update, fx, 0);
    br_system_add(world, "fx_draw", BR_PHASE_RENDER, sys_fx_draw, fx, 10);
}
