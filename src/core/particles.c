/* Particles: see particles.h.
 *
 * The component holds only what is saved; the particles themselves live in a
 * table attached to the world (one pt_emitter per entity, found by its index),
 * so copying or removing the component never shares or leaks them. The table
 * is tidied each step: an pt_emitter whose entity no longer has the component is
 * freed. */
#include "bronto/particles.h"
#include "bronto/components.h"
#include "bronto/components3d.h"
#include "bronto/hierarchy.h"
#include "bronto/assets.h"
#include "bronto/render3d.h"
#include "bronto/memory.h"

#include <math.h>
#include <string.h>

static const char *const k_particle_shapes[] = { "circle", "square", "image" };
static const char *const k_particle_blends[] = { "alpha", "none", "add", "multiply" };   /* br_blend_mode order */
static const br_field k_particle_fields[] = {
    BR_FIELD(br_particles, emitting, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_particles, rate, BR_FIELD_FLOAT, 0, 10000),
    BR_FIELD(br_particles, burst, BR_FIELD_INT, 0, 10000),
    BR_FIELD(br_particles, max, BR_FIELD_INT, 0, 10000),
    BR_FIELD(br_particles, lifetime, BR_FIELD_FLOAT, 0, 600),
    BR_FIELD(br_particles, lifetime_random, BR_FIELD_FLOAT, 0, 1),
    BR_FIELD(br_particles, speed, BR_FIELD_FLOAT, -100000, 100000),
    BR_FIELD(br_particles, speed_random, BR_FIELD_FLOAT, 0, 1),
    BR_FIELD(br_particles, angle, BR_FIELD_FLOAT, -360, 360),
    BR_FIELD(br_particles, spread, BR_FIELD_FLOAT, 0, 180),
    BR_FIELD(br_particles, area, BR_FIELD_VEC2, 0, 100000),
    BR_FIELD(br_particles, gravity, BR_FIELD_FLOAT, -100000, 100000),
    BR_FIELD(br_particles, drag, BR_FIELD_FLOAT, 0, 100),
    BR_FIELD(br_particles, size_start, BR_FIELD_FLOAT, 0, 4096),
    BR_FIELD(br_particles, size_end, BR_FIELD_FLOAT, 0, 4096),
    BR_FIELD(br_particles, color_start, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(br_particles, color_end, BR_FIELD_COLOR, 0, 0),
    BR_FIELD_ENUM_OF(br_particles, shape, k_particle_shapes),
    BR_FIELD(br_particles, image, BR_FIELD_STRING, 0, 0),
    BR_FIELD_ENUM_OF(br_particles, blend, k_particle_blends),
    BR_FIELD(br_particles, layer, BR_FIELD_INT, -100, 100),
    BR_FIELD(br_particles, local, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_particles, auto_destroy, BR_FIELD_BOOL, 0, 0),
};
static const br_particles k_particle_default = {
    true, 20, 0, 200, 1.0f, 0.25f, 60, 0.25f, -90, 25, { 0, 0 }, 0, 0, 4, 1,
    { 255, 220, 120, 255 }, { 255, 90, 40, 0 }, BR_PARTICLE_CIRCLE, "", BR_BLEND_ALPHA, 0, false, false
};
static const br_component_desc k_particle_desc = {
    "Particles", sizeof(br_particles), k_particle_fields, (int)BR_ARRAY_COUNT(k_particle_fields), &k_particle_default
};

/* ---- the table ------------------------------------------------------------------------- */

typedef struct pt_particle { br_vec3 p, v; float age, life; } pt_particle;

typedef struct pt_emitter {
    br_entity e;
    pt_particle *ps;
    int count, cap;
    float accum;                /* the part of a pt_particle owed by `rate` */
    float wait;                 /* preview: time since a burst-only pt_emitter went quiet */
    br_rng rng;
    bool was_emitting, gave_any, seen;
} pt_emitter;

typedef struct pt_state {
    pt_emitter *em;
    uint32_t count, cap;
    int32_t *slot;              /* entity index -> pt_emitter + 1 (0: none) */
    uint32_t slot_cap;
} pt_state;

static const char k_pt_state_key = 0;

static void pt_state_destroy(void *data) {
    pt_state *s = (pt_state *)data;
    uint32_t i;
    for (i = 0; i < s->count; ++i) br_free(s->em[i].ps);
    br_free(s->em);
    br_free(s->slot);
    br_free(s);
}

static pt_state *pt_state_of(const br_world *cw, bool make) {
    br_world *w = (br_world *)cw;
    pt_state *s = (pt_state *)br_world_attached(w, &k_pt_state_key);
    if (s || !make) return s;
    if (!(s = (pt_state *)br_alloc(sizeof(pt_state)))) return NULL;
    memset(s, 0, sizeof(*s));
    if (!br_world_attach(w, &k_pt_state_key, s, pt_state_destroy)) { br_free(s); return NULL; }
    return s;
}

static pt_emitter *pt_find(const pt_state *s, br_entity e) {
    uint32_t ix = BR_ENTITY_INDEX(e);
    pt_emitter *em;
    if (!s || ix >= s->slot_cap || !s->slot[ix]) return NULL;
    em = &s->em[s->slot[ix] - 1];
    return em->e == e ? em : NULL;
}

static pt_emitter *pt_find_or_make(pt_state *s, br_entity e) {
    uint32_t ix = BR_ENTITY_INDEX(e);
    pt_emitter *em = pt_find(s, e);
    if (em) return em;
    if (ix >= s->slot_cap) {
        uint32_t cap = s->slot_cap ? s->slot_cap : 256;
        int32_t *g;
        while (cap <= ix) cap *= 2;
        if (!(g = (int32_t *)br_realloc(s->slot, cap * sizeof(int32_t)))) return NULL;
        memset(g + s->slot_cap, 0, (cap - s->slot_cap) * sizeof(int32_t));
        s->slot = g;
        s->slot_cap = cap;
    }
    if (s->slot[ix]) {                                    /* an old entity's, with the same index */
        em = &s->em[s->slot[ix] - 1];
        em->count = 0;
    } else {
        if (s->count == s->cap) {
            uint32_t cap = s->cap ? s->cap * 2 : 16;
            pt_emitter *g = (pt_emitter *)br_realloc(s->em, cap * sizeof(pt_emitter));
            if (!g) return NULL;
            s->em = g;
            s->cap = cap;
        }
        em = &s->em[s->count++];
        memset(em, 0, sizeof(*em));
        s->slot[ix] = (int32_t)s->count;
    }
    {
        pt_particle *keep = em->ps;
        int cap = em->cap;
        memset(em, 0, sizeof(*em));
        em->ps = keep;
        em->cap = cap;
    }
    em->e = e;
    br_rng_seed(&em->rng, 0x9a7c1e5ull + (uint64_t)ix * 2654435761ull);   /* the same particles every run */
    return em;
}

static void pt_drop(pt_state *s, uint32_t i) {
    pt_emitter *em = &s->em[i];
    uint32_t ix = BR_ENTITY_INDEX(em->e);
    br_free(em->ps);
    if (ix < s->slot_cap) s->slot[ix] = 0;
    if (i != s->count - 1) {
        s->em[i] = s->em[s->count - 1];
        ix = BR_ENTITY_INDEX(s->em[i].e);
        if (ix < s->slot_cap) s->slot[ix] = (int32_t)i + 1;
    }
    --s->count;
}

void br_particles_clear(br_world *w, br_entity e) {
    pt_state *s = pt_state_of(w, false);
    uint32_t i;
    if (!s) return;
    if (e == BR_ENTITY_NULL) {                            /* everyone's: start again */
        while (s->count) pt_drop(s, s->count - 1);
        return;
    }
    for (i = 0; i < s->count; ++i)
        if (s->em[i].e == e) { s->em[i].count = 0; return; }
}

/* ---- giving off and moving --------------------------------------------------------------- */

typedef struct pt_where { bool is3d; br_vec3 origin; float rot; br_mat4 m; } pt_where;

static bool pt_where_of(const br_world *w, br_entity e, br_component t2, br_component t3, pt_where *out) {
    if (t3 != BR_COMPONENT_INVALID && br_has(w, e, t3)) {
        out->is3d = true;
        out->m = br_world_matrix3d(w, e);
        out->origin = br_m4_mul_point(out->m, br_v3(0, 0, 0));
        out->rot = 0;
        return true;
    }
    if (t2 != BR_COMPONENT_INVALID && br_has(w, e, t2)) {
        br_transform wt = br_world_transform(w, e);
        out->is3d = false;
        out->origin = br_v3(wt.position.x, wt.position.y, 0);
        out->rot = wt.rotation;
        return true;
    }
    return false;
}

static void pt_spawn(pt_emitter *em, const br_particles *P, const pt_where *at, int n) {
    int limit = P->max < 0 ? 0 : P->max;
    if (n > limit - em->count) n = limit - em->count;
    if (n <= 0) return;
    if (em->count + n > em->cap) {
        int cap = em->cap ? em->cap : 16;
        pt_particle *g;
        while (cap < em->count + n) cap *= 2;
        if (!(g = (pt_particle *)br_realloc(em->ps, (size_t)cap * sizeof(pt_particle)))) return;
        em->ps = g;
        em->cap = cap;
    }
    while (n-- > 0) {
        pt_particle *q = &em->ps[em->count];
        float r1 = br_rng_float(&em->rng), r2 = br_rng_float(&em->rng), r3 = br_rng_float(&em->rng);
        float r4 = br_rng_float(&em->rng), r5 = br_rng_float(&em->rng), r6 = br_rng_float(&em->rng);
        float speed = P->speed * (1 - r5 * P->speed_random);
        br_vec3 dir, off;
        q->life = P->lifetime * (1 - r6 * P->lifetime_random);
        q->age = 0;
        if (at->is3d) {
            float th = r1 * P->spread * BR_DEG2RAD, ph = r2 * BR_TAU;
            br_vec3 ld = br_v3(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
            dir = br_v3_norm(br_m4_mul_dir(at->m, ld));
            off = br_v3_sub(br_m4_mul_point(at->m, br_v3((r3 - 0.5f) * P->area.x, 0, (r4 - 0.5f) * P->area.y)), at->origin);
        } else {
            float a = (at->rot + P->angle + (r1 * 2 - 1) * P->spread) * BR_DEG2RAD;
            br_vec2 o = br_v2_rotate(br_v2((r3 - 0.5f) * P->area.x, (r4 - 0.5f) * P->area.y), at->rot * BR_DEG2RAD);
            BR_UNUSED(r2);
            dir = br_v3(cosf(a), sinf(a), 0);
            off = br_v3(o.x, o.y, 0);
        }
        if (q->life <= 0) continue;
        q->p = P->local ? off : br_v3_add(at->origin, off);
        q->v = br_v3_scale(dir, speed);
        ++em->count;
        em->gave_any = true;
    }
}

static void pt_move(pt_emitter *em, const br_particles *P, bool is3d, float dt) {
    br_vec3 fall = is3d ? br_v3(0, -P->gravity * dt, 0) : br_v3(0, P->gravity * dt, 0);
    float keep = P->drag > 0 ? (1 - P->drag * dt > 0 ? 1 - P->drag * dt : 0) : 1;
    int i, n = 0;
    for (i = 0; i < em->count; ++i) {                     /* (in order, so drawing order stays put) */
        pt_particle q = em->ps[i];
        q.age += dt;
        if (q.age >= q.life) continue;
        q.v = br_v3_scale(br_v3_add(q.v, fall), keep);
        q.p = br_v3_add(q.p, br_v3_scale(q.v, dt));
        em->ps[n++] = q;
    }
    em->count = n;
}

static void pt_step(br_world *w, float dt, bool preview) {
    br_component id = br_component_find(w, "Particles"), t2 = br_component_find(w, "Transform");
    br_ids3d ids3;
    br_component t3 = br_ids3d_of(w, &ids3) ? ids3.transform3d : BR_COMPONENT_INVALID;
    const br_component_pool *pool;
    pt_state *s;
    uint32_t i;
    if (id == BR_COMPONENT_INVALID) return;
    pool = &w->pools[id];
    s = pt_state_of(w, pool->count > 0);
    if (!s) return;
    for (i = 0; i < s->count; ++i) s->em[i].seen = false;
    for (i = 0; i < pool->count; ++i) {
        br_entity e = pool->entities[i];
        const br_particles *P = (const br_particles *)(pool->data + (size_t)i * pool->stride);
        pt_emitter *em = pt_find_or_make(s, e);
        pt_where at;
        if (!em) continue;
        em->seen = true;
        if (!pt_where_of(w, e, t2, t3, &at)) continue;
        pt_move(em, P, at.is3d, dt);
        if (P->emitting && !em->was_emitting) pt_spawn(em, P, &at, P->burst);   /* switched on: the burst */
        em->was_emitting = P->emitting;
        if (P->emitting && P->rate > 0) {
            int n;
            em->accum += P->rate * dt;
            n = (int)em->accum;
            em->accum -= (float)n;
            if (n > P->max) n = P->max;
            pt_spawn(em, P, &at, n);
        }
        if (!P->emitting || P->rate <= 0) {
            if (em->count == 0 && preview) {                   /* the editor: puff again now and then */
                em->wait += dt;
                if (em->wait > 0.6f) { em->wait = 0; pt_spawn(em, P, &at, P->burst > 0 ? P->burst : 0); }
            } else if (em->count == 0 && P->auto_destroy && em->gave_any) {
                br_entity_destroy_deferred(w, e);
            }
        }
    }
    for (i = s->count; i-- > 0;)
        if (!s->em[i].seen) pt_drop(s, i);
}

void br_particles_step(br_world *w, float dt) { pt_step(w, dt, false); }
void br_particles_preview(br_world *w, float dt) { pt_step(w, dt, true); }

bool br_particles_emit(br_world *w, br_entity e, int count) {
    br_component id = br_component_find(w, "Particles");
    br_ids3d ids3;
    const br_particles *P;
    pt_state *s;
    pt_emitter *em;
    pt_where at;
    if (id == BR_COMPONENT_INVALID || !(P = (const br_particles *)br_get(w, e, id))) return false;
    if (!pt_where_of(w, e, br_component_find(w, "Transform"), br_ids3d_of(w, &ids3) ? ids3.transform3d : BR_COMPONENT_INVALID, &at)) return false;
    if (!(s = pt_state_of(w, true)) || !(em = pt_find_or_make(s, e))) return false;
    if (!em->seen && em->count == 0) em->was_emitting = P->emitting;   /* new: emit() isn't also "switched on" */
    em->seen = true;
    pt_spawn(em, P, &at, count < 0 ? P->burst : count);
    return true;
}

int br_particles_count(const br_world *w, br_entity e) {
    const pt_emitter *em = pt_find(pt_state_of(w, false), e);
    return em ? em->count : 0;
}

/* ---- drawing ---------------------------------------------------------------------------- */

static br_blend_mode pt_blend_of(int b) { return b >= BR_BLEND_ALPHA && b <= BR_BLEND_MULTIPLY ? (br_blend_mode)b : BR_BLEND_ALPHA; }

static void pt_look(const br_particles *P, const pt_particle *q, float *size, br_color *c) {
    float t = q->life > 0 ? q->age / q->life : 1;
    if (t > 1) t = 1;
    *size = P->size_start + (P->size_end - P->size_start) * t;
    *c = br_color_lerp(P->color_start, P->color_end, t);
}

void br_particles_draw2d(const br_world *w, br_entity e, br_renderer *r) {
    br_component id = br_component_find(w, "Particles");
    const br_particles *P = id != BR_COMPONENT_INVALID ? (const br_particles *)br_get(w, e, id) : NULL;
    const pt_emitter *em = pt_find(pt_state_of(w, false), e);
    const br_image *img = NULL;
    br_vec2 base = br_v2(0, 0);
    int i;
    if (!P || !em || em->count == 0) return;
    if (P->local) base = br_world_transform(w, e).position;
    if (P->shape == BR_PARTICLE_IMAGE) img = br_asset_image(P->image);
    br_set_blend(r, pt_blend_of(P->blend));
    for (i = 0; i < em->count; ++i) {
        const pt_particle *q = &em->ps[i];
        br_vec2 p = br_v2(q->p.x + base.x, q->p.y + base.y);
        float size;
        br_color c;
        pt_look(P, q, &size, &c);
        if (c.a == 0 || size <= 0) continue;
        if (img && img->width > 0 && img->height > 0)
            br_draw_sprite(r, img, NULL, p, br_v2((float)img->width / 2, (float)img->height / 2),
                           br_v2(size / (float)img->width, size / (float)img->width), 0, c);
        else if (P->shape == BR_PARTICLE_CIRCLE && size > 2)
            br_fill_circle(r, p, size * 0.5f, c);
        else
            br_fill_rect(r, br_rect_make(p.x - size * 0.5f, p.y - size * 0.5f, size < 1 ? 1 : size, size < 1 ? 1 : size), c);
    }
    br_set_blend(r, BR_BLEND_ALPHA);
}

/* 3D: each pt_particle is a flat dot facing the camera, splatted straight into the
 * framebuffer at its depth (tested against the meshes, not written), so it hides
 * behind walls and glows through nothing. */
static void pt_splat(br_r3d *r3, br_recti box, float sx, float sy, float z, float rad, br_color c, int shape,
                  const br_image *img, br_blend_mode blend) {
    br_image *target = r3->r->target;
    int x0 = (int)floorf(sx - rad), x1 = (int)ceilf(sx + rad), y0 = (int)floorf(sy - rad), y1 = (int)ceilf(sy + rad), x, y;
    float inv = 1.0f / rad;
    if (x0 < box.x) x0 = box.x;
    if (y0 < box.y) y0 = box.y;
    if (x1 > box.x + box.w) x1 = box.x + box.w;
    if (y1 > box.y + box.h) y1 = box.y + box.h;
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x) {
            size_t at = (size_t)y * (size_t)target->width + (size_t)x;
            float dx = ((float)x + 0.5f - sx) * inv, dy = ((float)y + 0.5f - sy) * inv;
            br_color px = c;
            if (r3->depth && x < r3->depth_w && y < r3->depth_h && z > r3->depth[(size_t)y * (size_t)r3->depth_w + (size_t)x] + 2e-4f) continue;
            if (img) {
                int u = (int)((dx * 0.5f + 0.5f) * (float)img->width), v = (int)((dy * 0.5f + 0.5f) * (float)img->height);
                br_color t;
                if (u < 0 || v < 0 || u >= img->width || v >= img->height) continue;
                t = img->pixels[(size_t)v * (size_t)img->width + (size_t)u];
                px = br_rgba((uint8_t)(t.r * c.r / 255), (uint8_t)(t.g * c.g / 255), (uint8_t)(t.b * c.b / 255), (uint8_t)(t.a * c.a / 255));
            } else if (shape == BR_PARTICLE_CIRCLE) {
                float d2 = dx * dx + dy * dy;
                if (d2 > 1) continue;
                if (rad > 1.5f && d2 > 0.45f) px.a = (uint8_t)((float)px.a * (1 - d2) / 0.55f);   /* a soft edge */
            }
            br_blend_color(&target->pixels[at], px, blend);
        }
}

void br_particles_draw3d(const br_world *w, br_r3d *r3) {
    br_component id = br_component_find(w, "Particles");
    const pt_state *s = pt_state_of(w, false);
    br_ids3d ids3;
    const br_component_pool *pool;
    br_recti box;
    float focal;
    uint32_t i;
    if (id == BR_COMPONENT_INVALID || !s || !s->count || !r3->r || !r3->r->target || !br_ids3d_of(w, &ids3)) return;
    box = br_recti_intersect(r3->viewport, r3->r->clip);
    box = br_recti_intersect(box, br_recti_make(0, 0, r3->r->target->width, r3->r->target->height));
    if (box.w <= 0 || box.h <= 0) return;
    focal = r3->camera.orthographic ? (float)r3->viewport.h * 0.5f / (r3->camera.size > 1e-6f ? r3->camera.size : 1e-6f)
                                    : (float)r3->viewport.h * 0.5f / tanf(r3->camera.fov * 0.5f * BR_DEG2RAD);
    pool = &w->pools[id];
    for (i = 0; i < pool->count; ++i) {
        br_entity e = pool->entities[i];
        const br_particles *P = (const br_particles *)(pool->data + (size_t)i * pool->stride);
        const pt_emitter *em = pt_find(s, e);
        const br_image *img = P->shape == BR_PARTICLE_IMAGE ? br_asset_image(P->image) : NULL;
        br_vec3 base = br_v3(0, 0, 0);
        int k;
        if (!em || !em->count || !br_has(w, e, ids3.transform3d)) continue;
        if (img && (img->width <= 0 || img->height <= 0)) img = NULL;
        if (P->local) base = br_m4_mul_point(br_world_matrix3d(w, e), br_v3(0, 0, 0));
        for (k = 0; k < em->count; ++k) {
            const pt_particle *q = &em->ps[k];
            br_vec3 p = br_v3_add(q->p, base);
            br_vec4 c4 = br_m4_mul_v4(r3->view_proj, br_v4(p.x, p.y, p.z, 1));
            float size, sx, sy, z, rad;
            br_color c;
            if (c4.w <= 1e-4f) continue;
            pt_look(P, q, &size, &c);
            if (c.a == 0 || size <= 0) continue;
            sx = (float)r3->viewport.x + (c4.x / c4.w * 0.5f + 0.5f) * (float)r3->viewport.w;
            sy = (float)r3->viewport.y + (0.5f - c4.y / c4.w * 0.5f) * (float)r3->viewport.h;
            z = c4.z / c4.w * 0.5f + 0.5f;
            if (z < 0 || z > 1) continue;
            rad = size * 0.5f * (r3->camera.orthographic ? focal : focal / c4.w);
            if (rad < 0.5f) rad = 0.5f;
            if (r3->fog && r3->fog_end > r3->fog_start) {         /* fade into the fog, as the meshes do */
                float f = (br_v3_dist(p, r3->camera.position) - r3->fog_start) / (r3->fog_end - r3->fog_start);
                f = f < 0 ? 0 : f > 1 ? 1 : f;
                if (P->blend == BR_BLEND_ADD) c.a = (uint8_t)((float)c.a * (1 - f));
                else c = br_rgba((uint8_t)(c.r + (r3->fog_color.r - c.r) * f), (uint8_t)(c.g + (r3->fog_color.g - c.g) * f),
                                 (uint8_t)(c.b + (r3->fog_color.b - c.b) * f), c.a);
            }
            pt_splat(r3, box, sx, sy, z, rad, c, P->shape, img, pt_blend_of(P->blend));
        }
    }
}

/* ---- presets ---------------------------------------------------------------------------- */

static const char *const k_presets[] = { "sparks", "fire", "smoke", "explosion", "rain", "snow", "magic" };

const char *const *br_particles_presets(int *count) {
    if (count) *count = (int)BR_ARRAY_COUNT(k_presets);
    return k_presets;
}

static br_color pt_hex(uint32_t v) { return br_rgba((uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v); }

bool br_particles_preset(br_particles *P, const char *name, bool is3d) {
    float k = is3d ? 1.0f / 30.0f : 1.0f;                /* 2D values are in pixels; 3D in units */
    int which;
    for (which = 0; which < (int)BR_ARRAY_COUNT(k_presets); ++which)
        if (strcmp(name, k_presets[which]) == 0) break;
    if (which == (int)BR_ARRAY_COUNT(k_presets)) return false;
    *P = k_particle_default;
    P->burst = 0;
    P->angle = -90;
    switch (which) {
    case 0:   /* sparks */
        P->rate = 30; P->lifetime = 0.6f; P->lifetime_random = 0.4f; P->speed = 120; P->speed_random = 0.5f;
        P->spread = 40; P->gravity = 300; P->size_start = 3; P->size_end = 1;
        P->color_start = pt_hex(0xffe080ff); P->color_end = pt_hex(0xff602000);
        P->shape = BR_PARTICLE_SQUARE; P->blend = BR_BLEND_ADD;
        break;
    case 1:   /* fire */
        P->rate = 40; P->lifetime = 0.8f; P->lifetime_random = 0.4f; P->speed = 40; P->speed_random = 0.5f;
        P->spread = 15; P->area = br_v2(12, 2); P->gravity = -60; P->drag = 1; P->size_start = 10; P->size_end = 2;
        P->color_start = pt_hex(0xffd060ff); P->color_end = pt_hex(0xc0200000); P->blend = BR_BLEND_ADD;
        break;
    case 2:   /* smoke */
        P->rate = 12; P->lifetime = 2.5f; P->lifetime_random = 0.3f; P->speed = 25; P->speed_random = 0.5f;
        P->spread = 20; P->area = br_v2(8, 2); P->gravity = -10; P->drag = 0.5f; P->size_start = 6; P->size_end = 22;
        P->color_start = pt_hex(0x808080a0); P->color_end = pt_hex(0x60606000);
        break;
    case 3:   /* explosion */
        P->rate = 0; P->burst = 60; P->lifetime = 0.7f; P->lifetime_random = 0.5f; P->speed = 160; P->speed_random = 0.7f;
        P->spread = 180; P->gravity = 80; P->drag = 2; P->size_start = 8; P->size_end = 2;
        P->color_start = pt_hex(0xffe8a0ff); P->color_end = pt_hex(0xff400000); P->blend = BR_BLEND_ADD;
        break;
    case 4:   /* rain */
        P->rate = 80; P->max = 400; P->lifetime = 1.2f; P->lifetime_random = 0.1f; P->speed = 300; P->speed_random = 0.1f;
        P->angle = 100; P->spread = 2; P->area = br_v2(320, 0); P->size_start = P->size_end = 2;
        P->color_start = pt_hex(0xa0c0ffc0); P->color_end = pt_hex(0xa0c0ff80); P->shape = BR_PARTICLE_SQUARE;
        break;
    case 5:   /* snow */
        P->rate = 20; P->max = 300; P->lifetime = 6; P->lifetime_random = 0.3f; P->speed = 20; P->speed_random = 0.3f;
        P->angle = 90; P->spread = 30; P->area = br_v2(320, 0); P->gravity = 5; P->size_start = P->size_end = 3;
        P->color_start = pt_hex(0xffffffff); P->color_end = pt_hex(0xffffff00);
        break;
    default:  /* magic */
        P->rate = 25; P->lifetime = 1.2f; P->lifetime_random = 0.5f; P->speed = 15; P->speed_random = 0.5f;
        P->spread = 180; P->area = br_v2(16, 16); P->gravity = -20; P->size_start = 4; P->size_end = 0;
        P->color_start = pt_hex(0xb080ffff); P->color_end = pt_hex(0x60e0ff00); P->blend = BR_BLEND_ADD; P->local = true;
        break;
    }
    P->speed *= k; P->gravity *= k; P->size_start *= k; P->size_end *= k;
    P->area = br_v2(P->area.x * k, P->area.y * k);
    if (is3d && P->angle > 0) {                           /* rain and snow: down from above, over an area */
        P->speed = -P->speed;
        P->area = br_v2(20, 20);
    }
    if (is3d) P->size_start = BR_MAX(P->size_start, 0.04f);
    return true;
}

/* ---- registration ----------------------------------------------------------------------- */

static void pt_sys_particles(br_world *w, float dt, void *user) { BR_UNUSED(user); br_particles_step(w, dt); }

void br_particles_register(br_world *w) {
    br_component_register_desc(w, &k_particle_desc);
    br_system_add(w, "builtin_particles", BR_PHASE_FIXED_UPDATE, pt_sys_particles, NULL, 30);
}
