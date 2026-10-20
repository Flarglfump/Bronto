/*
 * Bronto Engine - 2D rigid-body physics (see physics.h).
 *
 * The solver follows the classic "sequential impulses" design (as in
 * Erin Catto's Box2D Lite): each step
 *
 *   1. gathers every entity with a Collider or a RigidBody into a body list,
 *   2. integrates forces (gravity, applied forces, damping) into velocities,
 *   3. finds candidate pairs by sorting bounding boxes along x (sort and sweep),
 *   4. computes contact manifolds: circle-circle, box-circle, and box-box by
 *      separating axes plus clipping the incident edge against the
 *      reference face (up to two points, each tagged with the edges that
 *      made it, so it can be recognised next step),
 *   5. warm-starts each contact with last step's impulses, then applies
 *      impulses several times so contacts stop sinking (normal) and sliding
 *      (friction, clamped by the Coulomb cone),
 *   6. integrates velocities into positions and writes Transforms back.
 *
 * Penetration is corrected with a velocity bias (Baumgarte) above a small
 * slop; bounce adds the restitution velocity on impact.
 *
 * Per-world state (last step's contacts and the event list) is attached to
 * the world with br_world_attach.
 */
#include "bronto/physics.h"
#include "bronto/components.h"
#include "bronto/hierarchy.h"
#include "bronto/memory.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SLOP 0.5f                  /* pixels of overlap allowed (keeps contacts stable) */
#define BIAS_FACTOR 0.2f
#define BOUNCE_THRESHOLD 40.0f     /* pixels/s: slower impacts don't bounce */
#define DEFAULT_GRAVITY_Y 600.0f
#define DEFAULT_ITERATIONS 10
#define DEG2RAD (BR_PI / 180.0f)

/* ---- descriptions --------------------------------------------------------- */

static const char *const k_body_types[] = { "dynamic", "static", "kinematic" };
static const br_field k_rigidbody_fields[] = {
    BR_FIELD_ENUM_OF(br_rigidbody, type, k_body_types),
    BR_FIELD(br_rigidbody, mass, BR_FIELD_FLOAT, 0.001f, 100000),
    BR_FIELD(br_rigidbody, gravity_scale, BR_FIELD_FLOAT, -10, 10),
    BR_FIELD(br_rigidbody, velocity, BR_FIELD_VEC2, 0, 0),
    BR_FIELD(br_rigidbody, angular_velocity, BR_FIELD_FLOAT, 0, 0),
    BR_FIELD(br_rigidbody, linear_damping, BR_FIELD_FLOAT, 0, 100),
    BR_FIELD(br_rigidbody, angular_damping, BR_FIELD_FLOAT, 0, 100),
    BR_FIELD(br_rigidbody, fixed_rotation, BR_FIELD_BOOL, 0, 0),
};
static const br_rigidbody k_rigidbody_default = { BR_BODY_DYNAMIC, 1, 1, { 0, 0 }, 0, 0, 0.05f, false, { 0, 0 }, 0 };

static const char *const k_collider_shapes[] = { "box", "circle" };
static const br_field k_collider_fields[] = {
    BR_FIELD_ENUM_OF(br_collider, shape, k_collider_shapes),
    BR_FIELD(br_collider, size, BR_FIELD_VEC2, 0, 100000),
    BR_FIELD(br_collider, radius, BR_FIELD_FLOAT, 0, 100000),
    BR_FIELD(br_collider, offset, BR_FIELD_VEC2, 0, 0),
    BR_FIELD(br_collider, friction, BR_FIELD_FLOAT, 0, 2),
    BR_FIELD(br_collider, bounce, BR_FIELD_FLOAT, 0, 1),
    BR_FIELD(br_collider, trigger, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_collider, layer, BR_FIELD_INT, 0, 31),
    BR_FIELD(br_collider, mask, BR_FIELD_INT, 0, 0),
};
static const br_collider k_collider_default = { BR_COLLIDER_BOX, { 16, 16 }, 8, { 0, 0 }, 0.5f, 0, false, 0, -1 };

static const br_field k_world_fields[] = {
    BR_FIELD(br_physics_world, gravity, BR_FIELD_VEC2, 0, 0),
    BR_FIELD(br_physics_world, iterations, BR_FIELD_INT, 1, 50),
};
static const br_physics_world k_world_default = { { 0, DEFAULT_GRAVITY_Y }, DEFAULT_ITERATIONS };

#define DESC(name, T, fields, def) { name, sizeof(T), fields, (int)BR_ARRAY_COUNT(fields), def }
static const br_component_desc k_phys_descs[] = {
    DESC("RigidBody", br_rigidbody, k_rigidbody_fields, &k_rigidbody_default),
    DESC("Collider", br_collider, k_collider_fields, &k_collider_default),
    DESC("PhysicsWorld", br_physics_world, k_world_fields, &k_world_default),
};
static const br_component_desc *const k_phys_desc_list[] = { &k_phys_descs[0], &k_phys_descs[1], &k_phys_descs[2] };

const br_component_desc *const *br_physics_descs(int *count) {
    *count = (int)BR_ARRAY_COUNT(k_phys_desc_list);
    return k_phys_desc_list;
}

typedef struct phys_ids { br_component transform, body, collider, world; } phys_ids;

static bool ph_ids_of(const br_world *w, phys_ids *id) {
    id->transform = br_component_find(w, "Transform");
    id->body = br_component_find(w, "RigidBody");
    id->collider = br_component_find(w, "Collider");
    id->world = br_component_find(w, "PhysicsWorld");
    return id->transform != BR_COMPONENT_INVALID && id->body != BR_COMPONENT_INVALID &&
           id->collider != BR_COMPONENT_INVALID && id->world != BR_COMPONENT_INVALID;
}

static void ph_body_added(br_world *w, br_entity e, void *data, void *user) {
    br_rigidbody *b = (br_rigidbody *)data;
    BR_UNUSED(w); BR_UNUSED(e); BR_UNUSED(user);
    b->force = br_v2(0, 0);
    b->torque = 0;
}

/* ---- small vector helpers ------------------------------------------------------ */

typedef struct rot { float c, s; } rot;                    /* a rotation as cos/sin */

static rot ph_rot_of(float radians) { rot r; r.c = cosf(radians); r.s = sinf(radians); return r; }
static br_vec2 ph_rot_apply(rot r, br_vec2 v) { return br_v2(r.c * v.x - r.s * v.y, r.s * v.x + r.c * v.y); }
static br_vec2 ph_rot_inv(rot r, br_vec2 v) { return br_v2(r.c * v.x + r.s * v.y, -r.s * v.x + r.c * v.y); }
static br_vec2 ph_rot_col1(rot r) { return br_v2(r.c, r.s); }      /* local x axis */
static br_vec2 ph_rot_col2(rot r) { return br_v2(-r.s, r.c); }     /* local y axis */
static float ph_cross2(br_vec2 a, br_vec2 b) { return a.x * b.y - a.y * b.x; }
static br_vec2 ph_cross_sv(float s, br_vec2 v) { return br_v2(-s * v.y, s * v.x); }
static float ph_dot2(br_vec2 a, br_vec2 b) { return a.x * b.x + a.y * b.y; }
static br_vec2 ph_sub2(br_vec2 a, br_vec2 b) { return br_v2(a.x - b.x, a.y - b.y); }
static br_vec2 ph_add2(br_vec2 a, br_vec2 b) { return br_v2(a.x + b.x, a.y + b.y); }
static br_vec2 ph_mul2(br_vec2 a, float s) { return br_v2(a.x * s, a.y * s); }
static float ph_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- bodies --------------------------------------------------------------------- */

typedef struct pbody {
    br_entity e;
    br_transform *t;
    br_rigidbody *rb;              /* NULL: static scenery */
    const br_collider *col;        /* NULL: moves, collides with nothing */
    int type;
    br_vec2 pos, v, scale;          /* world space */
    bool parented;
    uint32_t depth;                /* in the tree: parents are written back first */
    float angle, w;                /* radians, radians/s */
    float inv_mass, inv_i;
    /* shape in world space */
    rot q;
    br_vec2 center, half;          /* box half extents (scaled) */
    float radius;
    br_vec2 lo, hi;                /* bounding box */
} pbody;

#define FEATURE_NONE 0u
typedef struct contact {
    br_vec2 position, normal, r1, r2;
    float separation;
    float pn, pt;                  /* accumulated impulses */
    float mass_n, mass_t, bias, bounce;
    uint32_t feature;
} contact;

typedef struct arbiter {
    uint64_t key;                  /* the two entities, lower index first */
    int a, b;                      /* body indices this step */
    int count;
    contact c[2];
    float friction, bounce;
    bool trigger;
} arbiter;

typedef struct phys_state {
    pbody *bodies;
    uint32_t body_count, body_cap;
    int *order;                    /* sort and sweep */
    uint32_t order_cap;
    uint32_t max_depth;
    arbiter *arb[2];               /* this step's and last step's contacts */
    uint32_t arb_count[2], arb_cap[2];
    int cur;
    br_contact_event *events;
    uint32_t event_count, event_cap;
} phys_state;

static const char ph_state_key = 0;

static void ph_state_destroy(void *data) {
    phys_state *s = (phys_state *)data;
    br_free(s->bodies);
    br_free(s->order);
    br_free(s->arb[0]);
    br_free(s->arb[1]);
    br_free(s->events);
    br_free(s);
}

static phys_state *ph_state_of(br_world *w) {
    phys_state *s = (phys_state *)br_world_attached(w, &ph_state_key);
    if (s) return s;
    if (!(s = (phys_state *)br_alloc(sizeof(phys_state)))) return NULL;
    memset(s, 0, sizeof(*s));
    if (!br_world_attach(w, &ph_state_key, s, ph_state_destroy)) { br_free(s); return NULL; }
    return s;
}

static bool ph_grow(void **p, uint32_t *cap, uint32_t need, size_t item) {
    if (need <= *cap) return true;
    {
        uint32_t c = *cap ? *cap : 64;
        void *g;
        while (c < need) c *= 2;
        if (!(g = br_realloc(*p, (size_t)c * item))) return false;
        *p = g;
        *cap = c;
    }
    return true;
}

static void ph_push_event(phys_state *s, br_contact_kind kind, br_entity a, br_entity b, br_vec2 n, br_vec2 p, float speed) {
    br_contact_event *ev;
    if (!ph_grow((void **)&s->events, &s->event_cap, s->event_count + 1, sizeof(br_contact_event))) return;
    ev = &s->events[s->event_count++];
    ev->kind = kind;
    ev->a = a;
    ev->b = b;
    ev->normal = n;
    ev->point = p;
    ev->speed = speed;
}

/* Shape and bounding box from the body's current position. */
static void ph_update_shape(pbody *b) {
    const br_collider *c = b->col;
    br_vec2 sc = br_v2(br_absf(b->scale.x), br_absf(b->scale.y));
    b->q = ph_rot_of(b->angle);
    if (!c) { b->lo = b->hi = b->pos; return; }
    b->center = ph_add2(b->pos, ph_rot_apply(b->q, br_v2(c->offset.x * b->scale.x, c->offset.y * b->scale.y)));
    if (c->shape == BR_COLLIDER_CIRCLE) {
        b->radius = c->radius * BR_MAX(sc.x, sc.y);
        b->lo = ph_sub2(b->center, br_v2(b->radius, b->radius));
        b->hi = ph_add2(b->center, br_v2(b->radius, b->radius));
    } else {
        float ex, ey;
        b->half = br_v2(c->size.x * sc.x / 2, c->size.y * sc.y / 2);
        ex = br_absf(b->q.c) * b->half.x + br_absf(b->q.s) * b->half.y;
        ey = br_absf(b->q.s) * b->half.x + br_absf(b->q.c) * b->half.y;
        b->lo = ph_sub2(b->center, br_v2(ex, ey));
        b->hi = ph_add2(b->center, br_v2(ex, ey));
    }
}

static void ph_gather(br_world *w, const phys_ids *id, phys_state *s) {
    br_component_pool *pools[2];
    int k;
    uint32_t i;
    s->body_count = 0;
    s->max_depth = 0;
    pools[0] = &w->pools[id->collider];
    pools[1] = &w->pools[id->body];
    for (k = 0; k < 2; ++k) {                       /* colliders, then bodies without one */
        for (i = 0; i < pools[k]->count; ++i) {
            br_entity e = pools[k]->entities[i];
            br_transform *t = (br_transform *)br_get(w, e, id->transform);
            pbody *b;
            if (!t || (k == 1 && br_has(w, e, id->collider))) continue;
            if (!ph_grow((void **)&s->bodies, &s->body_cap, s->body_count + 1, sizeof(pbody))) return;
            b = &s->bodies[s->body_count++];
            memset(b, 0, sizeof(*b));
            b->e = e;
            b->t = t;
            b->rb = (br_rigidbody *)br_get(w, e, id->body);
            b->col = (const br_collider *)br_get(w, e, id->collider);
            b->type = b->rb ? b->rb->type : BR_BODY_STATIC;
            {   /* the world transform: colliders on children sit where the tree puts them */
                br_transform wt = br_world_transform(w, e);
                b->pos = wt.position;
                b->angle = wt.rotation * DEG2RAD;
                b->scale = wt.scale;
                b->parented = br_parent_of(w, e) != BR_ENTITY_NULL;
                b->depth = b->parented ? br_depth(w, e) : 0;
                if (b->depth > s->max_depth) s->max_depth = b->depth;
            }
            if (b->rb) {
                b->v = b->rb->velocity;
                b->w = b->rb->angular_velocity * DEG2RAD;
            }
            ph_update_shape(b);
            if (b->type == BR_BODY_DYNAMIC) {
                float m = b->rb->mass > 0 ? b->rb->mass : 1, inertia;
                b->inv_mass = 1.0f / m;
                if (b->col && b->col->shape == BR_COLLIDER_CIRCLE) inertia = m * b->radius * b->radius / 2;
                else if (b->col) inertia = m * (4 * b->half.x * b->half.x + 4 * b->half.y * b->half.y) / 12;
                else inertia = m * 64;
                b->inv_i = b->rb->fixed_rotation || inertia <= 0 ? 0 : 1.0f / inertia;
            }
        }
    }
}

/* ---- narrow phase ------------------------------------------------------------------ */

/* Contact features: which edges of the two boxes made a point. */
enum { NO_EDGE = 0, EDGE1, EDGE2, EDGE3, EDGE4 };
#define FEAT(in1, out1, in2, out2) ((uint32_t)(in1) | ((uint32_t)(out1) << 8) | ((uint32_t)(in2) << 16) | ((uint32_t)(out2) << 24))
#define F_IN1(f) ((f) & 0xFFu)
#define F_OUT1(f) (((f) >> 8) & 0xFFu)
#define F_IN2(f) (((f) >> 16) & 0xFFu)
#define F_OUT2(f) (((f) >> 24) & 0xFFu)
static uint32_t ph_feat_flip(uint32_t f) { return FEAT(F_IN2(f), F_OUT2(f), F_IN1(f), F_OUT1(f)); }

typedef struct clip_vertex { br_vec2 v; uint32_t fp; } clip_vertex;

static int ph_clip_segment(clip_vertex out[2], const clip_vertex in[2], br_vec2 normal, float offset, int clip_edge) {
    int n = 0;
    float d0 = ph_dot2(normal, in[0].v) - offset, d1 = ph_dot2(normal, in[1].v) - offset;
    if (d0 <= 0) out[n++] = in[0];
    if (d1 <= 0) out[n++] = in[1];
    if (d0 * d1 < 0 && n < 2) {
        float t = d0 / (d0 - d1);
        out[n].v = ph_add2(in[0].v, ph_mul2(ph_sub2(in[1].v, in[0].v), t));
        if (d0 > 0) out[n].fp = FEAT(clip_edge, F_OUT1(in[0].fp), NO_EDGE, F_OUT2(in[0].fp));
        else out[n].fp = FEAT(F_IN1(in[1].fp), clip_edge, F_IN2(in[1].fp), NO_EDGE);
        ++n;
    }
    return n;
}

static void ph_incident_edge(clip_vertex c[2], br_vec2 h, br_vec2 pos, rot q, br_vec2 normal) {
    br_vec2 n = ph_rot_inv(q, ph_mul2(normal, -1));             /* the normal in the incident box's frame */
    if (br_absf(n.x) > br_absf(n.y)) {
        if (n.x > 0) {
            c[0].v = br_v2(h.x, -h.y); c[0].fp = FEAT(NO_EDGE, NO_EDGE, EDGE3, EDGE4);
            c[1].v = br_v2(h.x, h.y);  c[1].fp = FEAT(NO_EDGE, NO_EDGE, EDGE4, EDGE1);
        } else {
            c[0].v = br_v2(-h.x, h.y);  c[0].fp = FEAT(NO_EDGE, NO_EDGE, EDGE1, EDGE2);
            c[1].v = br_v2(-h.x, -h.y); c[1].fp = FEAT(NO_EDGE, NO_EDGE, EDGE2, EDGE3);
        }
    } else {
        if (n.y > 0) {
            c[0].v = br_v2(h.x, h.y);  c[0].fp = FEAT(NO_EDGE, NO_EDGE, EDGE4, EDGE1);
            c[1].v = br_v2(-h.x, h.y); c[1].fp = FEAT(NO_EDGE, NO_EDGE, EDGE1, EDGE2);
        } else {
            c[0].v = br_v2(-h.x, -h.y); c[0].fp = FEAT(NO_EDGE, NO_EDGE, EDGE2, EDGE3);
            c[1].v = br_v2(h.x, -h.y);  c[1].fp = FEAT(NO_EDGE, NO_EDGE, EDGE3, EDGE4);
        }
    }
    c[0].v = ph_add2(pos, ph_rot_apply(q, c[0].v));
    c[1].v = ph_add2(pos, ph_rot_apply(q, c[1].v));
}

/* Box A vs box B: up to 2 points; the normal points from A to B. */
static int ph_collide_boxes(contact *out, const pbody *A, const pbody *B) {
    enum { FACE_A_X, FACE_A_Y, FACE_B_X, FACE_B_Y } axis;
    br_vec2 hA = A->half, hB = B->half, pA = A->center, pB = B->center;
    br_vec2 dp = ph_sub2(pB, pA), dA = ph_rot_inv(A->q, dp), dB = ph_rot_inv(B->q, dp);
    /* C = RotA^T RotB, as its absolute values */
    float c11 = A->q.c * B->q.c + A->q.s * B->q.s, c12 = -A->q.c * B->q.s + A->q.s * B->q.c;
    float c21 = -A->q.s * B->q.c + A->q.c * B->q.s, c22 = A->q.s * B->q.s + A->q.c * B->q.c;
    float a11 = br_absf(c11), a12 = br_absf(c12), a21 = br_absf(c21), a22 = br_absf(c22);
    br_vec2 faceA = br_v2(br_absf(dA.x) - hA.x - (a11 * hB.x + a12 * hB.y), br_absf(dA.y) - hA.y - (a21 * hB.x + a22 * hB.y));
    br_vec2 faceB = br_v2(br_absf(dB.x) - (a11 * hA.x + a21 * hA.y) - hB.x, br_absf(dB.y) - (a12 * hA.x + a22 * hA.y) - hB.y);
    br_vec2 normal, front_normal, side_normal;
    float separation, front, side, neg_side, pos_side;
    int neg_edge, pos_edge, i, n = 0;
    clip_vertex inc[2], clip1[2], clip2[2];
    const float rel = 0.95f, abs_tol = 0.01f;
    if (faceA.x > 0 || faceA.y > 0 || faceB.x > 0 || faceB.y > 0) return 0;
    axis = FACE_A_X;
    separation = faceA.x;
    normal = dA.x > 0 ? ph_rot_col1(A->q) : ph_mul2(ph_rot_col1(A->q), -1);
    if (faceA.y > rel * separation + abs_tol * hA.y) {
        axis = FACE_A_Y; separation = faceA.y;
        normal = dA.y > 0 ? ph_rot_col2(A->q) : ph_mul2(ph_rot_col2(A->q), -1);
    }
    if (faceB.x > rel * separation + abs_tol * hB.x) {
        axis = FACE_B_X; separation = faceB.x;
        normal = dB.x > 0 ? ph_rot_col1(B->q) : ph_mul2(ph_rot_col1(B->q), -1);
    }
    if (faceB.y > rel * separation + abs_tol * hB.y) {
        axis = FACE_B_Y; separation = faceB.y;
        normal = dB.y > 0 ? ph_rot_col2(B->q) : ph_mul2(ph_rot_col2(B->q), -1);
    }
    switch (axis) {
    case FACE_A_X:
        front_normal = normal; front = ph_dot2(pA, front_normal) + hA.x;
        side_normal = ph_rot_col2(A->q); side = ph_dot2(pA, side_normal);
        neg_side = -side + hA.y; pos_side = side + hA.y; neg_edge = EDGE3; pos_edge = EDGE1;
        ph_incident_edge(inc, hB, pB, B->q, front_normal);
        break;
    case FACE_A_Y:
        front_normal = normal; front = ph_dot2(pA, front_normal) + hA.y;
        side_normal = ph_rot_col1(A->q); side = ph_dot2(pA, side_normal);
        neg_side = -side + hA.x; pos_side = side + hA.x; neg_edge = EDGE2; pos_edge = EDGE4;
        ph_incident_edge(inc, hB, pB, B->q, front_normal);
        break;
    case FACE_B_X:
        front_normal = ph_mul2(normal, -1); front = ph_dot2(pB, front_normal) + hB.x;
        side_normal = ph_rot_col2(B->q); side = ph_dot2(pB, side_normal);
        neg_side = -side + hB.y; pos_side = side + hB.y; neg_edge = EDGE3; pos_edge = EDGE1;
        ph_incident_edge(inc, hA, pA, A->q, front_normal);
        break;
    default:
        front_normal = ph_mul2(normal, -1); front = ph_dot2(pB, front_normal) + hB.y;
        side_normal = ph_rot_col1(B->q); side = ph_dot2(pB, side_normal);
        neg_side = -side + hB.x; pos_side = side + hB.x; neg_edge = EDGE2; pos_edge = EDGE4;
        ph_incident_edge(inc, hA, pA, A->q, front_normal);
        break;
    }
    if (ph_clip_segment(clip1, inc, ph_mul2(side_normal, -1), neg_side, neg_edge) < 2) return 0;
    if (ph_clip_segment(clip2, clip1, side_normal, pos_side, pos_edge) < 2) return 0;
    for (i = 0; i < 2; ++i) {
        float sep = ph_dot2(front_normal, clip2[i].v) - front;
        if (sep <= 0) {
            out[n].separation = sep;
            out[n].normal = normal;
            out[n].position = ph_sub2(clip2[i].v, ph_mul2(front_normal, sep));   /* onto the reference face */
            out[n].feature = (axis == FACE_B_X || axis == FACE_B_Y) ? ph_feat_flip(clip2[i].fp) : clip2[i].fp;
            ++n;
        }
    }
    return n;
}

static int ph_collide_circles(contact *out, const pbody *A, const pbody *B) {
    br_vec2 d = ph_sub2(B->center, A->center);
    float dist2 = ph_dot2(d, d), r = A->radius + B->radius, dist;
    if (dist2 > r * r) return 0;
    dist = sqrtf(dist2);
    out->normal = dist > 1e-6f ? ph_mul2(d, 1.0f / dist) : br_v2(0, 1);
    out->separation = dist - r;
    out->position = ph_add2(A->center, ph_mul2(out->normal, A->radius + out->separation / 2));
    out->feature = 1;
    return 1;
}

/* Box A vs circle B. */
static int ph_collide_box_circle(contact *out, const pbody *A, const pbody *B) {
    br_vec2 local = ph_rot_inv(A->q, ph_sub2(B->center, A->center)), closest, n;
    br_vec2 h = A->half;
    float sep;
    bool inside = br_absf(local.x) <= h.x && br_absf(local.y) <= h.y;
    if (inside) {                                      /* push out through the nearest face */
        float dx = h.x - br_absf(local.x), dy = h.y - br_absf(local.y);
        if (dx < dy) { n = br_v2(local.x < 0 ? -1.0f : 1.0f, 0); closest = br_v2(local.x < 0 ? -h.x : h.x, local.y); sep = -dx - B->radius; }
        else { n = br_v2(0, local.y < 0 ? -1.0f : 1.0f); closest = br_v2(local.x, local.y < 0 ? -h.y : h.y); sep = -dy - B->radius; }
    } else {
        br_vec2 d;
        float dist;
        closest = br_v2(ph_clampf(local.x, -h.x, h.x), ph_clampf(local.y, -h.y, h.y));
        d = ph_sub2(local, closest);
        dist = sqrtf(ph_dot2(d, d));
        if (dist > B->radius) return 0;
        n = dist > 1e-6f ? ph_mul2(d, 1.0f / dist) : br_v2(0, 1);
        sep = dist - B->radius;
    }
    out->normal = ph_rot_apply(A->q, n);
    out->separation = sep;
    out->position = ph_add2(A->center, ph_rot_apply(A->q, closest));
    out->feature = 2;
    return 1;
}

static int ph_collide(contact *out, const pbody *A, const pbody *B) {
    bool ca = A->col->shape == BR_COLLIDER_CIRCLE, cb = B->col->shape == BR_COLLIDER_CIRCLE;
    if (ca && cb) return ph_collide_circles(out, A, B);
    if (!ca && !cb) return ph_collide_boxes(out, A, B);
    if (!ca) return ph_collide_box_circle(out, A, B);
    {                                                  /* circle A, box B: solve as box-circle, flip */
        int n = ph_collide_box_circle(out, B, A);
        if (n) out->normal = ph_mul2(out->normal, -1);
        return n;
    }
}

/* ---- arbiters ------------------------------------------------------------------------ */

static uint64_t ph_pair_key(br_entity a, br_entity b) { return ((uint64_t)a << 32) | (uint64_t)b; }

static int ph_compare_arbiters(const void *x, const void *y) {
    uint64_t a = ((const arbiter *)x)->key, b = ((const arbiter *)y)->key;
    return a < b ? -1 : a > b ? 1 : 0;
}

static const arbiter *ph_find_arbiter(const arbiter *list, uint32_t count, uint64_t key) {
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (list[mid].key < key) lo = mid + 1;
        else hi = mid;
    }
    return lo < count && list[lo].key == key ? &list[lo] : NULL;
}

static bool ph_layers_match(const br_collider *a, const br_collider *b) {
    uint32_t la = 1u << (a->layer & 31), lb = 1u << (b->layer & 31);
    return ((uint32_t)a->mask & lb) && ((uint32_t)b->mask & la);
}

static const pbody *ph_sort_bodies;
static int ph_compare_lo(const void *x, const void *y) {
    float a = ph_sort_bodies[*(const int *)x].lo.x, b = ph_sort_bodies[*(const int *)y].lo.x;
    if (a != b) return a < b ? -1 : 1;
    return *(const int *)x - *(const int *)y;
}

static void ph_find_pairs(phys_state *s) {
    uint32_t i, j, n = s->body_count;
    int cur = s->cur;
    s->arb_count[cur] = 0;
    if (!ph_grow((void **)&s->order, &s->order_cap, n, sizeof(int))) return;
    for (i = 0; i < n; ++i) s->order[i] = (int)i;
    ph_sort_bodies = s->bodies;
    if (n > 1) qsort(s->order, n, sizeof(int), ph_compare_lo);
    for (i = 0; i < n; ++i) {
        const pbody *A = &s->bodies[s->order[i]];
        if (!A->col) continue;
        for (j = i + 1; j < n; ++j) {
            const pbody *B = &s->bodies[s->order[j]];
            const pbody *p, *q;
            arbiter *arb;
            contact c[2];
            int count;
            bool trigger;
            if (B->lo.x > A->hi.x) break;                       /* sorted: nothing further overlaps */
            if (!B->col || B->lo.y > A->hi.y || B->hi.y < A->lo.y) continue;
            if (A->inv_mass == 0 && B->inv_mass == 0 && !A->col->trigger && !B->col->trigger &&
                A->type != BR_BODY_KINEMATIC && B->type != BR_BODY_KINEMATIC) continue;   /* two immovable things */
            if (!ph_layers_match(A->col, B->col)) continue;
            memset(c, 0, sizeof(c));                            /* impulses start at zero */
            p = BR_ENTITY_INDEX(A->e) < BR_ENTITY_INDEX(B->e) ? A : B;   /* a stable order for the pair */
            q = p == A ? B : A;
            count = ph_collide(c, p, q);
            if (!count) continue;
            trigger = p->col->trigger || q->col->trigger;
            if (!ph_grow((void **)&s->arb[cur], &s->arb_cap[cur], s->arb_count[cur] + 1, sizeof(arbiter))) return;
            arb = &s->arb[cur][s->arb_count[cur]++];
            memset(arb, 0, sizeof(*arb));
            arb->key = ph_pair_key(p->e, q->e);
            arb->a = (int)(p - s->bodies);
            arb->b = (int)(q - s->bodies);
            arb->count = count;
            arb->c[0] = c[0];
            if (count > 1) arb->c[1] = c[1];
            arb->trigger = trigger;
            arb->friction = sqrtf(BR_MAX(0.0f, p->col->friction) * BR_MAX(0.0f, q->col->friction));
            arb->bounce = BR_MAX(p->col->bounce, q->col->bounce);
        }
    }
    if (s->arb_count[cur] > 1) qsort(s->arb[cur], s->arb_count[cur], sizeof(arbiter), ph_compare_arbiters);
}

/* Carry impulses over from last step (warm starting) and report what changed. */
static void ph_match_and_report(phys_state *s) {
    int cur = s->cur, prev = 1 - cur;
    uint32_t i;
    int k, m;
    s->event_count = 0;
    for (i = 0; i < s->arb_count[cur]; ++i) {
        arbiter *a = &s->arb[cur][i];
        const arbiter *old = ph_find_arbiter(s->arb[prev], s->arb_count[prev], a->key);
        const pbody *A = &s->bodies[a->a], *B = &s->bodies[a->b];
        if (old && !a->trigger)
            for (k = 0; k < a->count; ++k)
                for (m = 0; m < old->count; ++m)
                    if (a->c[k].feature == old->c[m].feature) { a->c[k].pn = old->c[m].pn; a->c[k].pt = old->c[m].pt; }
        if (!old) {
            if (a->trigger) ph_push_event(s, BR_TRIGGER_ENTER, A->e, B->e, a->c[0].normal, a->c[0].position, 0);
            else {
                br_vec2 dv = ph_sub2(ph_add2(B->v, ph_cross_sv(B->w, ph_sub2(a->c[0].position, B->pos))),
                                  ph_add2(A->v, ph_cross_sv(A->w, ph_sub2(a->c[0].position, A->pos))));
                ph_push_event(s, BR_CONTACT_BEGIN, A->e, B->e, a->c[0].normal, a->c[0].position, BR_MAX(0.0f, -ph_dot2(dv, a->c[0].normal)));
            }
        }
    }
    for (i = 0; i < s->arb_count[prev]; ++i) {
        const arbiter *old = &s->arb[prev][i];
        if (!ph_find_arbiter(s->arb[cur], s->arb_count[cur], old->key))
            ph_push_event(s, old->trigger ? BR_TRIGGER_EXIT : BR_CONTACT_END, (br_entity)(old->key >> 32),
                       (br_entity)(old->key & 0xFFFFFFFFu), br_v2(0, 0), br_v2(0, 0), 0);
    }
}

static void ph_pre_step(phys_state *s, arbiter *arb, float inv_dt) {
    pbody *A = &s->bodies[arb->a], *B = &s->bodies[arb->b];
    int i;
    for (i = 0; i < arb->count; ++i) {
        contact *c = &arb->c[i];
        br_vec2 t = br_v2(c->normal.y, -c->normal.x), dv;
        float rn1, rn2, rt1, rt2, kn, kt, vn;
        c->r1 = ph_sub2(c->position, A->pos);
        c->r2 = ph_sub2(c->position, B->pos);
        rn1 = ph_dot2(c->r1, c->normal); rn2 = ph_dot2(c->r2, c->normal);
        kn = A->inv_mass + B->inv_mass + A->inv_i * (ph_dot2(c->r1, c->r1) - rn1 * rn1) + B->inv_i * (ph_dot2(c->r2, c->r2) - rn2 * rn2);
        c->mass_n = kn > 0 ? 1.0f / kn : 0;
        rt1 = ph_dot2(c->r1, t); rt2 = ph_dot2(c->r2, t);
        kt = A->inv_mass + B->inv_mass + A->inv_i * (ph_dot2(c->r1, c->r1) - rt1 * rt1) + B->inv_i * (ph_dot2(c->r2, c->r2) - rt2 * rt2);
        c->mass_t = kt > 0 ? 1.0f / kt : 0;
        c->bias = -BIAS_FACTOR * inv_dt * BR_MIN(0.0f, c->separation + SLOP);
        dv = ph_sub2(ph_add2(B->v, ph_cross_sv(B->w, c->r2)), ph_add2(A->v, ph_cross_sv(A->w, c->r1)));
        vn = ph_dot2(dv, c->normal);
        c->bounce = vn < -BOUNCE_THRESHOLD ? -arb->bounce * vn : 0;
        {   /* warm start */
            br_vec2 P = ph_add2(ph_mul2(c->normal, c->pn), ph_mul2(t, c->pt));
            A->v = ph_sub2(A->v, ph_mul2(P, A->inv_mass));
            A->w -= A->inv_i * ph_cross2(c->r1, P);
            B->v = ph_add2(B->v, ph_mul2(P, B->inv_mass));
            B->w += B->inv_i * ph_cross2(c->r2, P);
        }
    }
}

static void ph_apply_impulses(phys_state *s, arbiter *arb) {
    pbody *A = &s->bodies[arb->a], *B = &s->bodies[arb->b];
    int i;
    for (i = 0; i < arb->count; ++i) {
        contact *c = &arb->c[i];
        br_vec2 t = br_v2(c->normal.y, -c->normal.x), dv, P;
        float vn, dpn, pn0, vt, dpt, max_pt, pt0;
        dv = ph_sub2(ph_add2(B->v, ph_cross_sv(B->w, c->r2)), ph_add2(A->v, ph_cross_sv(A->w, c->r1)));
        vn = ph_dot2(dv, c->normal);
        dpn = c->mass_n * (-vn + BR_MAX(c->bias, c->bounce));
        pn0 = c->pn;
        c->pn = BR_MAX(pn0 + dpn, 0.0f);
        dpn = c->pn - pn0;
        P = ph_mul2(c->normal, dpn);
        A->v = ph_sub2(A->v, ph_mul2(P, A->inv_mass)); A->w -= A->inv_i * ph_cross2(c->r1, P);
        B->v = ph_add2(B->v, ph_mul2(P, B->inv_mass)); B->w += B->inv_i * ph_cross2(c->r2, P);

        dv = ph_sub2(ph_add2(B->v, ph_cross_sv(B->w, c->r2)), ph_add2(A->v, ph_cross_sv(A->w, c->r1)));
        vt = ph_dot2(dv, t);
        dpt = c->mass_t * -vt;
        max_pt = arb->friction * c->pn;
        pt0 = c->pt;
        c->pt = ph_clampf(pt0 + dpt, -max_pt, max_pt);
        dpt = c->pt - pt0;
        P = ph_mul2(t, dpt);
        A->v = ph_sub2(A->v, ph_mul2(P, A->inv_mass)); A->w -= A->inv_i * ph_cross2(c->r1, P);
        B->v = ph_add2(B->v, ph_mul2(P, B->inv_mass)); B->w += B->inv_i * ph_cross2(c->r2, P);
    }
}

/* ---- the step ------------------------------------------------------------------------- */

br_vec2 br_physics_gravity(const br_world *w) {
    phys_ids id;
    if (ph_ids_of(w, &id) && w->pools[id.world].count > 0) return ((const br_physics_world *)w->pools[id.world].data)->gravity;
    return br_v2(0, DEFAULT_GRAVITY_Y);
}

void br_physics_step(br_world *w, float dt) {
    phys_ids id;
    phys_state *s;
    br_vec2 gravity = br_v2(0, DEFAULT_GRAVITY_Y);
    int iterations = DEFAULT_ITERATIONS, it;
    uint32_t i, d;
    float inv_dt;
    if (dt <= 0 || !ph_ids_of(w, &id) || !(s = ph_state_of(w))) return;
    if (w->pools[id.world].count > 0) {
        const br_physics_world *pw = (const br_physics_world *)w->pools[id.world].data;
        gravity = pw->gravity;
        iterations = pw->iterations > 0 ? pw->iterations : 1;
    }
    inv_dt = 1.0f / dt;
    ph_gather(w, &id, s);

    for (i = 0; i < s->body_count; ++i) {                  /* forces -> velocities */
        pbody *b = &s->bodies[i];
        if (b->type != BR_BODY_DYNAMIC) continue;
        b->v = ph_add2(b->v, ph_mul2(ph_add2(ph_mul2(gravity, b->rb->gravity_scale), ph_mul2(b->rb->force, b->inv_mass)), dt));
        b->w += dt * b->inv_i * b->rb->torque;
        b->v = ph_mul2(b->v, 1.0f / (1.0f + dt * b->rb->linear_damping));
        b->w *= 1.0f / (1.0f + dt * b->rb->angular_damping);
    }

    s->cur = 1 - s->cur;
    ph_find_pairs(s);
    ph_match_and_report(s);
    for (i = 0; i < s->arb_count[s->cur]; ++i)
        if (!s->arb[s->cur][i].trigger) ph_pre_step(s, &s->arb[s->cur][i], inv_dt);
    for (it = 0; it < iterations; ++it)
        for (i = 0; i < s->arb_count[s->cur]; ++i)
            if (!s->arb[s->cur][i].trigger) ph_apply_impulses(s, &s->arb[s->cur][i]);

    for (d = 0; d <= s->max_depth; ++d)                  /* velocities -> positions, parents first */
    for (i = 0; i < s->body_count; ++i) {
        pbody *b = &s->bodies[i];
        if (!b->rb || b->depth != d) continue;
        if (b->type != BR_BODY_STATIC) {
            b->pos = ph_add2(b->pos, ph_mul2(b->v, dt));
            b->angle += b->w * dt;
            if (b->parented) {                                /* back into the parent's space */
                br_transform wt;
                wt.position = b->pos;
                wt.rotation = fmodf(b->angle / DEG2RAD, 360.0f);
                wt.scale = b->scale;
                *b->t = br_local_from_world(w, b->e, &wt);
            } else {
                b->t->position = b->pos;
                b->t->rotation = fmodf(b->angle / DEG2RAD, 360.0f);
            }
            b->rb->velocity = b->v;
            b->rb->angular_velocity = b->w / DEG2RAD;
        }
        b->rb->force = br_v2(0, 0);
        b->rb->torque = 0;
    }
}

const br_contact_event *br_physics_events(const br_world *w, uint32_t *count) {
    const phys_state *s = (const phys_state *)br_world_attached(w, &ph_state_key);
    *count = s ? s->event_count : 0;
    return s ? s->events : NULL;
}

/* ---- pushing things ------------------------------------------------------------------- */

static br_rigidbody *ph_dynamic_body(br_world *w, br_entity e, br_transform **t) {
    phys_ids id;
    br_rigidbody *rb;
    if (!ph_ids_of(w, &id) || !(rb = (br_rigidbody *)br_get(w, e, id.body)) || rb->type != BR_BODY_DYNAMIC) return NULL;
    *t = (br_transform *)br_get(w, e, id.transform);
    return *t ? rb : NULL;
}

void br_physics_apply_force(br_world *w, br_entity e, br_vec2 force, const br_vec2 *point) {
    br_transform *t;
    br_rigidbody *rb = ph_dynamic_body(w, e, &t);
    if (!rb) return;
    rb->force = ph_add2(rb->force, force);
    if (point) rb->torque += ph_cross2(ph_sub2(*point, t->position), force);
}

void br_physics_apply_impulse(br_world *w, br_entity e, br_vec2 impulse, const br_vec2 *point) {
    br_transform *t;
    br_rigidbody *rb = ph_dynamic_body(w, e, &t);
    float m;
    if (!rb) return;
    m = rb->mass > 0 ? rb->mass : 1;
    rb->velocity = ph_add2(rb->velocity, ph_mul2(impulse, 1.0f / m));
    if (point && !rb->fixed_rotation) {               /* treat it as a unit box for spin: a rough, stable choice */
        phys_ids id;
        const br_collider *c;
        float inertia = m * 64;
        ph_ids_of(w, &id);
        if ((c = (const br_collider *)br_get(w, e, id.collider)) != NULL)
            inertia = c->shape == BR_COLLIDER_CIRCLE ? m * c->radius * c->radius / 2 : m * (c->size.x * c->size.x + c->size.y * c->size.y) / 12;
        if (inertia > 0) rb->angular_velocity += ph_cross2(ph_sub2(*point, t->position), impulse) / inertia / DEG2RAD;
    }
}

/* ---- queries --------------------------------------------------------------------------- */

/* A collider's shape now, for queries and drawing (no step state needed). */
static bool ph_shape_now(const br_world *w, const phys_ids *id, uint32_t i, pbody *b) {
    const br_component_pool *p = &w->pools[id->collider];
    br_entity e = p->entities[i];
    memset(b, 0, sizeof(*b));
    b->t = (br_transform *)br_get(w, e, id->transform);
    if (!b->t) return false;
    b->e = e;
    b->col = (const br_collider *)(p->data + (size_t)i * p->stride);
    {
        br_transform wt = br_world_transform(w, e);
        b->pos = wt.position;
        b->angle = wt.rotation * DEG2RAD;
        b->scale = wt.scale;
    }
    ph_update_shape(b);
    return true;
}

static bool ph_ray_shape(const pbody *b, br_vec2 from, br_vec2 to, float *t_out, br_vec2 *n_out) {
    if (b->col->shape == BR_COLLIDER_CIRCLE) {
        br_vec2 d = ph_sub2(to, from), f = ph_sub2(from, b->center);
        float a = ph_dot2(d, d), bb = 2 * ph_dot2(f, d), c = ph_dot2(f, f) - b->radius * b->radius, disc, t;
        if (a <= 0) return false;
        disc = bb * bb - 4 * a * c;
        if (disc < 0) return false;
        t = (-bb - sqrtf(disc)) / (2 * a);
        if (t < 0 || t > 1) return false;
        *t_out = t;
        *n_out = ph_mul2(ph_sub2(ph_add2(from, ph_mul2(d, t)), b->center), 1.0f / b->radius);
        return true;
    } else {                                                    /* slabs, in the box's frame */
        br_vec2 o = ph_rot_inv(b->q, ph_sub2(from, b->center)), d = ph_rot_inv(b->q, ph_sub2(to, from)), n = br_v2(0, 0);
        float tmin = 0, tmax = 1;
        int axis;
        for (axis = 0; axis < 2; ++axis) {
            float oa = axis ? o.y : o.x, da = axis ? d.y : d.x, h = axis ? b->half.y : b->half.x;
            if (br_absf(da) < 1e-9f) { if (oa < -h || oa > h) return false; continue; }
            {
                float t1 = (-h - oa) / da, t2 = (h - oa) / da, sgn = -1;
                if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; sgn = 1; }
                if (t1 > tmin) { tmin = t1; n = axis ? br_v2(0, sgn) : br_v2(sgn, 0); }
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) return false;
            }
        }
        if (tmin <= 0 && (n.x == 0 && n.y == 0)) return false;     /* starts inside */
        *t_out = tmin;
        *n_out = ph_rot_apply(b->q, n);
        return true;
    }
}

bool br_physics_raycast(const br_world *w, br_vec2 from, br_vec2 to, int mask, br_entity ignore, br_raycast_hit *hit) {
    phys_ids id;
    uint32_t i;
    bool found = false;
    float best = 2;
    if (!ph_ids_of(w, &id)) return false;
    for (i = 0; i < w->pools[id.collider].count; ++i) {
        pbody b;
        float t;
        br_vec2 n;
        if (!ph_shape_now(w, &id, i, &b) || b.e == ignore || b.col->trigger) continue;
        if (!((uint32_t)mask & (1u << (b.col->layer & 31)))) continue;
        if (ph_ray_shape(&b, from, to, &t, &n) && t < best) {
            best = t;
            found = true;
            if (hit) {
                hit->entity = b.e;
                hit->fraction = t;
                hit->point = ph_add2(from, ph_mul2(ph_sub2(to, from), t));
                hit->normal = n;
            }
        }
    }
    return found;
}

br_entity br_physics_point(const br_world *w, br_vec2 point, int mask) {
    phys_ids id;
    uint32_t i;
    if (!ph_ids_of(w, &id)) return BR_ENTITY_NULL;
    for (i = 0; i < w->pools[id.collider].count; ++i) {
        pbody b;
        if (!ph_shape_now(w, &id, i, &b) || !((uint32_t)mask & (1u << (b.col->layer & 31)))) continue;
        if (b.col->shape == BR_COLLIDER_CIRCLE) {
            br_vec2 d = ph_sub2(point, b.center);
            if (ph_dot2(d, d) <= b.radius * b.radius) return b.e;
        } else {
            br_vec2 l = ph_rot_inv(b.q, ph_sub2(point, b.center));
            if (br_absf(l.x) <= b.half.x && br_absf(l.y) <= b.half.y) return b.e;
        }
    }
    return BR_ENTITY_NULL;
}

void br_physics_debug_draw(const br_world *w, br_renderer *r, br_color solid, br_color trigger) {
    phys_ids id;
    uint32_t i;
    if (!ph_ids_of(w, &id)) return;
    for (i = 0; i < w->pools[id.collider].count; ++i) {
        pbody b;
        br_color c;
        if (!ph_shape_now(w, &id, i, &b)) continue;
        c = b.col->trigger ? trigger : solid;
        if (b.col->shape == BR_COLLIDER_CIRCLE) {
            br_vec2 edge = ph_add2(b.center, ph_rot_apply(b.q, br_v2(b.radius, 0)));
            br_draw_circle(r, b.center, b.radius, c);
            br_draw_line(r, b.center, edge, c);              /* shows the rotation */
        } else {
            br_vec2 p[4];
            int k;
            p[0] = ph_add2(b.center, ph_rot_apply(b.q, br_v2(-b.half.x, -b.half.y)));
            p[1] = ph_add2(b.center, ph_rot_apply(b.q, br_v2(b.half.x, -b.half.y)));
            p[2] = ph_add2(b.center, ph_rot_apply(b.q, br_v2(b.half.x, b.half.y)));
            p[3] = ph_add2(b.center, ph_rot_apply(b.q, br_v2(-b.half.x, b.half.y)));
            for (k = 0; k < 4; ++k) br_draw_line(r, p[k], p[(k + 1) % 4], c);
        }
    }
}

/* ---- registration ------------------------------------------------------------------------ */

static void ph_sys_physics(br_world *w, float dt, void *user) {
    BR_UNUSED(user);
    br_physics_step(w, dt);
}

void br_physics_register(br_world *w) {
    int i;
    for (i = 0; i < (int)BR_ARRAY_COUNT(k_phys_descs); ++i) br_component_register_desc(w, &k_phys_descs[i]);
    br_component_set_hooks(w, br_component_find(w, "RigidBody"), ph_body_added, NULL, NULL);
    br_system_add(w, "builtin_physics", BR_PHASE_FIXED_UPDATE, ph_sys_physics, NULL, 20);
}
