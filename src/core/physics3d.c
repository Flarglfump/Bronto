/*
 * Bronto Engine - 3D rigid-body physics (see physics3d.h).
 *
 * The same "sequential impulses" design as the 2D solver (physics.c), in
 * three dimensions: bodies carry a position (their centre of mass: the
 * collider's centre), a quaternion, linear and angular velocity, and an
 * inverse inertia tensor turned into the world each step. Box-box contacts
 * use the separating axis test over the 15 candidate axes (preferring face
 * axes), then clip the incident face against the reference face's side
 * planes; edge-edge contacts take the closest points of the two edges.
 */
#include "bronto/physics3d.h"
#include "bronto/components3d.h"
#include "bronto/hierarchy.h"
#include "bronto/memory.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define P3_SLOP 0.005f                /* units of overlap allowed */
#define P3_BAUMGARTE 0.2f
#define P3_BOUNCE_SPEED 1.0f          /* units / second: slower impacts don't bounce */
#define P3_DEFAULT_ITERATIONS 10
#define P3_MAX_CONTACTS 4

/* ---- descriptions ----------------------------------------------------------------- */

static const char *const k_body3_types[] = { "dynamic", "static", "kinematic" };
static const br_field k_rb3_fields[] = {
    BR_FIELD_ENUM_OF(br_rigidbody3d, type, k_body3_types),
    BR_FIELD(br_rigidbody3d, mass, BR_FIELD_FLOAT, 0.001f, 100000),
    BR_FIELD(br_rigidbody3d, gravity_scale, BR_FIELD_FLOAT, -10, 10),
    BR_FIELD(br_rigidbody3d, velocity, BR_FIELD_VEC3, 0, 0),
    BR_FIELD(br_rigidbody3d, angular_velocity, BR_FIELD_VEC3, 0, 0),
    BR_FIELD(br_rigidbody3d, linear_damping, BR_FIELD_FLOAT, 0, 100),
    BR_FIELD(br_rigidbody3d, angular_damping, BR_FIELD_FLOAT, 0, 100),
    BR_FIELD(br_rigidbody3d, lock_rotation, BR_FIELD_BOOL, 0, 0),
};
static const br_rigidbody3d k_rb3_default = { BR_BODY_DYNAMIC, 1, 1, { 0, 0, 0 }, { 0, 0, 0 }, 0, 0.05f, false, { 0, 0, 0 }, { 0, 0, 0 } };

static const char *const k_col3_shapes[] = { "box", "sphere", "capsule", "mesh" };
static const br_field k_col3_fields[] = {
    BR_FIELD_ENUM_OF(br_collider3d, shape, k_col3_shapes),
    BR_FIELD(br_collider3d, size, BR_FIELD_VEC3, 0, 100000),
    BR_FIELD(br_collider3d, radius, BR_FIELD_FLOAT, 0, 100000),
    BR_FIELD(br_collider3d, offset, BR_FIELD_VEC3, 0, 0),
    BR_FIELD(br_collider3d, friction, BR_FIELD_FLOAT, 0, 2),
    BR_FIELD(br_collider3d, bounce, BR_FIELD_FLOAT, 0, 1),
    BR_FIELD(br_collider3d, trigger, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_collider3d, layer, BR_FIELD_INT, 0, 31),
    BR_FIELD(br_collider3d, mask, BR_FIELD_INT, 0, 0),
};
static const br_collider3d k_col3_default = { BR_COLLIDER3D_BOX, { 1, 1, 1 }, 0.5f, { 0, 0, 0 }, 0.5f, 0, false, 0, -1 };

static const br_field k_world3_fields[] = {
    BR_FIELD(br_physics_world3d, gravity, BR_FIELD_VEC3, 0, 0),
    BR_FIELD(br_physics_world3d, iterations, BR_FIELD_INT, 1, 50),
};
static const br_physics_world3d k_world3_default = { { 0, -9.81f, 0 }, P3_DEFAULT_ITERATIONS };

#define P3DESC(name, T, fields, def) { name, sizeof(T), fields, (int)BR_ARRAY_COUNT(fields), def }
static const br_component_desc k_p3_descs[] = {
    P3DESC("RigidBody3D", br_rigidbody3d, k_rb3_fields, &k_rb3_default),
    P3DESC("Collider3D", br_collider3d, k_col3_fields, &k_col3_default),
    P3DESC("PhysicsWorld3D", br_physics_world3d, k_world3_fields, &k_world3_default),
};
static const br_component_desc *const k_p3_desc_list[] = { &k_p3_descs[0], &k_p3_descs[1], &k_p3_descs[2] };

const br_component_desc *const *br_physics3d_descs(int *count) {
    *count = (int)BR_ARRAY_COUNT(k_p3_desc_list);
    return k_p3_desc_list;
}

typedef struct p3_ids { br_component t3, body, col, world; } p3_ids;
static bool p3_ids_of(const br_world *w, p3_ids *id) {
    id->t3 = br_component_find(w, "Transform3D");
    id->body = br_component_find(w, "RigidBody3D");
    id->col = br_component_find(w, "Collider3D");
    id->world = br_component_find(w, "PhysicsWorld3D");
    return id->t3 != BR_COMPONENT_INVALID && id->body != BR_COMPONENT_INVALID && id->col != BR_COMPONENT_INVALID &&
           id->world != BR_COMPONENT_INVALID;
}

/* ---- 3x3 matrices (rows) ------------------------------------------------------------- */

typedef struct m3 { float m[9]; } m3;
static m3 m3_from_quat(br_quat q) {
    br_mat4 r = br_quat_to_m4(q);
    m3 o;
    int row, col;
    for (row = 0; row < 3; ++row)
        for (col = 0; col < 3; ++col) o.m[row * 3 + col] = r.m[col * 4 + row];
    return o;
}
static br_vec3 m3_mul(const m3 *a, br_vec3 v) {
    return br_v3(a->m[0] * v.x + a->m[1] * v.y + a->m[2] * v.z, a->m[3] * v.x + a->m[4] * v.y + a->m[5] * v.z,
                 a->m[6] * v.x + a->m[7] * v.y + a->m[8] * v.z);
}
static br_vec3 m3_col(const m3 *a, int c) { return br_v3(a->m[c], a->m[3 + c], a->m[6 + c]); }
/* R diag(d) R^T */
static m3 m3_rdrt(const m3 *r, br_vec3 d) {
    m3 o;
    int i, j;
    float dd[3];
    dd[0] = d.x; dd[1] = d.y; dd[2] = d.z;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            o.m[i * 3 + j] = r->m[i * 3] * dd[0] * r->m[j * 3] + r->m[i * 3 + 1] * dd[1] * r->m[j * 3 + 1] + r->m[i * 3 + 2] * dd[2] * r->m[j * 3 + 2];
    return o;
}
static float v3c(br_vec3 v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }
static float br_maxf(float a, float b) { return a > b ? a : b; }

/* ---- bodies, contacts, per-world state ------------------------------------------------------ */

typedef struct p3body {
    br_entity e;
    br_rigidbody3d *rb;
    br_collider3d *col;
    br_transform3d *t;
    int type;
    br_vec3 x;                  /* centre of mass (the collider's centre) */
    br_quat q;
    br_vec3 scale, offset_local;
    br_vec3 v, w;               /* w: radians / second */
    float inv_mass;
    br_vec3 inv_i_local;
    m3 inv_i;                   /* world */
    m3 R;
    br_vec3 ax[3], half;        /* box axes and half extents (world) */
    float radius;
    int shape;
    br_vec3 seg_a, seg_b;       /* capsule: the segment its radius is around (world) */
    const br_mesh *mesh;        /* mesh: the entity's Mesh, its vertices in the world at mv */
    const br_vec3 *mv;
    br_mat4 to_local;           /* mesh: from the world into the mesh's own space (for its BVH) */
    uint32_t mv_at;             /* (where they are in the state's buffer, while gathering) */
    bool owns_mv;               /* (a query's own copy: p3_done frees it) */
    br_vec3 lo, hi;             /* bounds */
    bool trigger, parented, has_collider;
    int layer, mask;
    float friction, bounce;
    uint32_t depth;
} p3body;

typedef struct p3contact {
    br_vec3 p;
    float depth;
    uint32_t id;
    br_vec3 ra, rb;
    float mn, mt1, mt2, bias, pn, pt1, pt2;
} p3contact;

typedef struct p3arb {
    br_entity ea, eb;
    uint32_t a, b;
    br_vec3 n, t1, t2;
    p3contact c[P3_MAX_CONTACTS];
    int count;
    float friction, bounce;
    bool trigger;
    float speed;                /* approach speed when it began */
} p3arb;

typedef struct p3state {
    p3body *bodies;
    uint32_t body_count, body_cap;
    p3arb *arb[2];
    uint32_t arb_count[2], arb_cap[2];
    int cur;
    br_contact3d_event *events;
    uint32_t event_count, event_cap;
    uint32_t *order, order_cap;
    uint32_t max_depth;
    br_vec3 *mesh_verts;        /* mesh colliders' vertices, in the world, this step */
    uint32_t mesh_vert_count, mesh_vert_cap;
    br_physics3d_move_info *moved;   /* what br_physics3d_move found, per entity */
    uint32_t moved_count, moved_cap;
} p3state;

static const int p3_state_key = 0;

static void p3_state_free(void *d) {
    p3state *s = (p3state *)d;
    br_free(s->bodies);
    br_free(s->arb[0]);
    br_free(s->arb[1]);
    br_free(s->events);
    br_free(s->order);
    br_free(s->mesh_verts);
    br_free(s->moved);
    br_free(s);
}

static p3state *p3_state(br_world *w) {
    p3state *s = (p3state *)br_world_attached(w, &p3_state_key);
    if (s) return s;
    if (!(s = BR_NEW(p3state))) return NULL;
    if (!br_world_attach(w, &p3_state_key, s, p3_state_free)) { br_free(s); return NULL; }
    return s;
}

static bool grow(void **p, uint32_t *cap, uint32_t need, size_t elem) {
    uint32_t c;
    void *g;
    if (need <= *cap) return true;
    c = *cap ? *cap * 2 : 64;
    while (c < need) c *= 2;
    if (!(g = br_realloc(*p, elem * c))) return false;
    *p = g;
    *cap = c;
    return true;
}

static void p3_event(p3state *s, br_contact_kind kind, br_entity a, br_entity b, br_vec3 n, br_vec3 p, float speed) {
    br_contact3d_event *ev;
    if (!grow((void **)&s->events, &s->event_cap, s->event_count + 1, sizeof(br_contact3d_event))) return;
    ev = &s->events[s->event_count++];
    ev->kind = kind; ev->a = a; ev->b = b; ev->normal = n; ev->point = p; ev->speed = speed;
}

/* ---- gathering ---------------------------------------------------------------------------- */

static const br_mesh *p3_mesh_of(const br_world *w, br_entity e) {
    br_ids3d ids;
    const br_mesh3d *m;
    if (!br_ids3d_of(w, &ids) || !(m = (const br_mesh3d *)br_get(w, e, ids.mesh))) return NULL;
    return br_mesh3d_mesh(m);
}

static void p3_mesh_world(const br_mesh *m, br_mat4 world, br_vec3 *out) {
    uint32_t i;
    for (i = 0; i < m->vert_count; ++i) out[i] = br_m4_mul_point(world, m->verts[i].pos);
}

static void p3_shape(p3body *b) {
    int k;
    b->R = m3_from_quat(b->q);
    for (k = 0; k < 3; ++k) b->ax[k] = m3_col(&b->R, k);
    if (b->shape == BR_COLLIDER3D_CAPSULE) {
        br_vec3 r = br_v3(b->radius, b->radius, b->radius);
        float h = br_maxf(0, b->half.y - b->radius);           /* half the segment: the height less the caps */
        b->seg_a = br_v3_add_scaled(b->x, b->ax[1], -h);
        b->seg_b = br_v3_add_scaled(b->x, b->ax[1], h);
        b->lo = br_v3_sub(br_v3_min(b->seg_a, b->seg_b), r);
        b->hi = br_v3_add(br_v3_max(b->seg_a, b->seg_b), r);
    } else if (b->shape == BR_COLLIDER3D_MESH) {
        uint32_t i;
        b->lo = b->hi = b->x;
        if (b->mv && b->mesh && b->mesh->vert_count) {
            b->lo = b->hi = b->mv[0];
            for (i = 1; i < b->mesh->vert_count; ++i) { b->lo = br_v3_min(b->lo, b->mv[i]); b->hi = br_v3_max(b->hi, b->mv[i]); }
        }
    } else if (b->shape == BR_COLLIDER3D_SPHERE) {
        b->lo = br_v3_sub(b->x, br_v3(b->radius, b->radius, b->radius));
        b->hi = br_v3_add(b->x, br_v3(b->radius, b->radius, b->radius));
    } else {
        br_vec3 r = br_v3(0, 0, 0);
        for (k = 0; k < 3; ++k) {
            float h = v3c(b->half, k);
            r.x += fabsf(b->ax[k].x) * h; r.y += fabsf(b->ax[k].y) * h; r.z += fabsf(b->ax[k].z) * h;
        }
        b->lo = br_v3_sub(b->x, r);
        b->hi = br_v3_add(b->x, r);
    }
    b->inv_i = b->inv_mass > 0 ? m3_rdrt(&b->R, b->inv_i_local) : m3_rdrt(&b->R, br_v3(0, 0, 0));
}

static void p3_gather(br_world *w, const p3_ids *id, p3state *s) {
    const br_component_pool *pc = &w->pools[id->col], *pb = &w->pools[id->body];
    uint32_t i, k;
    s->body_count = 0;
    s->max_depth = 0;
    s->mesh_vert_count = 0;
    for (k = 0; k < 2; ++k) {                       /* colliders, then bodies without one */
        const br_component_pool *pool = k == 0 ? pc : pb;
        for (i = 0; i < pool->count; ++i) {
            br_entity e = pool->entities[i];
            p3body *b;
            br_vec3 pos, sc, absc;
            br_quat q;
            float m;
            if (k == 1 && br_has(w, e, id->col)) continue;
            if (!br_has(w, e, id->t3)) continue;
            if (!grow((void **)&s->bodies, &s->body_cap, s->body_count + 1, sizeof(p3body))) return;
            b = &s->bodies[s->body_count++];
            memset(b, 0, sizeof(*b));
            b->e = e;
            b->t = (br_transform3d *)br_get(w, e, id->t3);
            b->rb = (br_rigidbody3d *)br_get(w, e, id->body);
            b->col = (br_collider3d *)br_get(w, e, id->col);
            b->has_collider = b->col != NULL;
            b->parented = br_parent_of(w, e) != BR_ENTITY_NULL;
            b->depth = br_depth(w, e);
            if (b->depth > s->max_depth) s->max_depth = b->depth;
            br_world_transform3d(w, e, &pos, &q, &sc);
            b->q = br_quat_norm(q);
            b->scale = sc;
            absc = br_v3(fabsf(sc.x), fabsf(sc.y), fabsf(sc.z));
            b->type = b->rb ? b->rb->type : BR_BODY_STATIC;
            if (b->col) {
                b->shape = b->col->shape;
                b->offset_local = br_v3_mul(b->col->offset, sc);
                b->half = br_v3_scale(br_v3_mul(br_v3(fabsf(b->col->size.x), fabsf(b->col->size.y), fabsf(b->col->size.z)), absc), 0.5f);
                b->radius = fabsf(b->col->radius) * BR_MAX(absc.x, BR_MAX(absc.y, absc.z));
                b->trigger = b->col->trigger;
                b->layer = b->col->layer;
                b->mask = b->col->mask;
                b->friction = b->col->friction;
                b->bounce = b->col->bounce;
                if (b->shape == BR_COLLIDER3D_CAPSULE) b->radius = fabsf(b->col->radius) * BR_MAX(absc.x, absc.z);
                if (b->shape == BR_COLLIDER3D_MESH) {            /* the entity's Mesh, in the world */
                    b->mesh = p3_mesh_of(w, e);
                    if (b->type == BR_BODY_DYNAMIC) b->type = BR_BODY_STATIC;   /* (meshes never fall: scenery, or kinematic) */
                    if (b->mesh && grow((void **)&s->mesh_verts, &s->mesh_vert_cap, s->mesh_vert_count + b->mesh->vert_count, sizeof(br_vec3))) {
                        b->mv_at = s->mesh_vert_count;
                        br_mat4 world = br_world_matrix3d(w, e);
                        p3_mesh_world(b->mesh, world, s->mesh_verts + s->mesh_vert_count);
                        b->to_local = br_m4_inverse(world);
                        s->mesh_vert_count += b->mesh->vert_count;
                    } else {
                        b->mesh = NULL;
                    }
                }
            } else {
                b->shape = BR_COLLIDER3D_SPHERE;
                b->radius = 0;
            }
            b->x = br_v3_add(pos, br_quat_rotate(b->q, b->offset_local));
            if (b->rb) {
                b->v = b->rb->velocity;
                b->w = br_v3_scale(b->rb->angular_velocity, BR_DEG2RAD);
            }
            m = b->rb ? (b->rb->mass > 0.001f ? b->rb->mass : 0.001f) : 0;
            if (b->type == BR_BODY_DYNAMIC) {
                b->inv_mass = 1.0f / m;
                if (b->rb->lock_rotation) b->inv_i_local = br_v3(0, 0, 0);
                else if (b->shape == BR_COLLIDER3D_SPHERE) {
                    float r = b->radius > 1e-4f ? b->radius : 0.5f, ii = 1.0f / (0.4f * m * r * r);
                    b->inv_i_local = br_v3(ii, ii, ii);
                } else if (b->shape == BR_COLLIDER3D_CAPSULE) {  /* (as a solid cylinder of its full height) */
                    float r = b->radius > 1e-4f ? b->radius : 0.5f, h = br_maxf(2 * b->half.y, 2 * r);
                    float side = 1.0f / (m * (3 * r * r + h * h) / 12.0f);
                    b->inv_i_local = br_v3(side, 1.0f / (0.5f * m * r * r), side);
                } else {
                    float x = 2 * b->half.x, y = 2 * b->half.y, z = 2 * b->half.z;
                    x = x > 1e-3f ? x : 1e-3f; y = y > 1e-3f ? y : 1e-3f; z = z > 1e-3f ? z : 1e-3f;
                    b->inv_i_local = br_v3(12.0f / (m * (y * y + z * z)), 12.0f / (m * (x * x + z * z)), 12.0f / (m * (x * x + y * y)));
                }
            }
        }
    }
    for (i = 0; i < s->body_count; ++i) {                    /* (the vertex buffer has stopped moving now) */
        p3body *b = &s->bodies[i];
        if (b->shape == BR_COLLIDER3D_MESH && b->mesh) b->mv = s->mesh_verts + b->mv_at;
        p3_shape(b);
    }
}

/* ---- narrow phase ------------------------------------------------------------------------------ */

static int sphere_sphere(const p3body *a, const p3body *b, br_vec3 *n, p3contact *c) {
    br_vec3 d = br_v3_sub(b->x, a->x);
    float dist = br_v3_len(d), r = a->radius + b->radius;
    if (dist > r) return 0;
    *n = dist > 1e-6f ? br_v3_scale(d, 1.0f / dist) : br_v3(0, 1, 0);
    c->p = br_v3_add(a->x, br_v3_scale(*n, a->radius - (r - dist) * 0.5f));
    c->depth = r - dist;
    c->id = 0;
    return 1;
}

/* box a, sphere b: the normal points from the box to the sphere */
static int box_sphere(const p3body *a, const p3body *b, br_vec3 *n, p3contact *c) {
    br_vec3 d = br_v3_sub(b->x, a->x), l, q;
    float e[3], lc[3], qc[3];
    int k, inside_axis = -1;
    float best = 1e30f, dist;
    l = br_v3(br_v3_dot(d, a->ax[0]), br_v3_dot(d, a->ax[1]), br_v3_dot(d, a->ax[2]));
    e[0] = a->half.x; e[1] = a->half.y; e[2] = a->half.z;
    lc[0] = l.x; lc[1] = l.y; lc[2] = l.z;
    for (k = 0; k < 3; ++k) qc[k] = br_clampf(lc[k], -e[k], e[k]);
    q = br_v3(qc[0], qc[1], qc[2]);
    if (lc[0] != qc[0] || lc[1] != qc[1] || lc[2] != qc[2]) {        /* outside */
        br_vec3 diff = br_v3_sub(l, q), wq;
        dist = br_v3_len(diff);
        if (dist > b->radius) return 0;
        diff = dist > 1e-6f ? br_v3_scale(diff, 1.0f / dist) : br_v3(0, 1, 0);
        *n = br_v3_norm(br_v3_add(br_v3_add(br_v3_scale(a->ax[0], diff.x), br_v3_scale(a->ax[1], diff.y)), br_v3_scale(a->ax[2], diff.z)));
        wq = br_v3_add(a->x, br_v3_add(br_v3_add(br_v3_scale(a->ax[0], q.x), br_v3_scale(a->ax[1], q.y)), br_v3_scale(a->ax[2], q.z)));
        c->p = wq;
        c->depth = b->radius - dist;
        c->id = 0;
        return 1;
    }
    for (k = 0; k < 3; ++k) {                                          /* the centre is inside: the nearest face */
        float gap = e[k] - fabsf(lc[k]);
        if (gap < best) { best = gap; inside_axis = k; }
    }
    *n = br_v3_scale(a->ax[inside_axis], lc[inside_axis] < 0 ? -1.0f : 1.0f);
    c->p = br_v3_sub(b->x, br_v3_scale(*n, b->radius));
    c->depth = b->radius + best;
    c->id = 1;
    return 1;
}

/* Clips polygon `in` (n points) to the half-space dot(p, pn) <= off. */
typedef struct cpt { br_vec3 p; uint32_t id; } cpt;
static int clip_plane(const cpt *in, int n, br_vec3 pn, float off, cpt *out, uint32_t plane) {
    int i, k = 0;
    for (i = 0; i < n; ++i) {
        const cpt *a = &in[i], *b = &in[(i + 1) % n];
        float da = br_v3_dot(a->p, pn) - off, db = br_v3_dot(b->p, pn) - off;
        if (da <= 0) out[k++] = *a;
        if ((da <= 0) != (db <= 0) && k < 16) {
            cpt x;
            x.p = br_v3_lerp(a->p, b->p, da / (da - db));
            x.id = 0x40u | (plane << 2) | (uint32_t)i;
            out[k++] = x;
        }
    }
    return k;
}

/* Reference box `ref` (its face `axis`, pointing along nref toward the
 * other box), incident box `inc`: up to four contact points. */
static int face_contacts(const p3body *ref, int axis, br_vec3 nref, const p3body *inc, p3contact *c, uint32_t idbase) {
    cpt poly[16], tmp[16];
    br_vec3 rc, ic, u, v;
    float best = -1, hu, hv, deepest = -1;
    int k, kk = 0, n, i, a1, a2, side;
    float s = 1;
    int count = 0, pick[4], chosen = 0;
    float depth[16];
    for (k = 0; k < 3; ++k) {                                 /* the incident face: the most opposed */
        float dd = fabsf(br_v3_dot(inc->ax[k], nref));
        if (dd > best) { best = dd; kk = k; }
    }
    s = br_v3_dot(inc->ax[kk], nref) > 0 ? -1.0f : 1.0f;
    ic = br_v3_add(inc->x, br_v3_scale(inc->ax[kk], s * v3c(inc->half, kk)));
    u = inc->ax[(kk + 1) % 3]; hu = v3c(inc->half, (kk + 1) % 3);
    v = inc->ax[(kk + 2) % 3]; hv = v3c(inc->half, (kk + 2) % 3);
    poly[0].p = br_v3_add(ic, br_v3_add(br_v3_scale(u, hu), br_v3_scale(v, hv)));
    poly[1].p = br_v3_add(ic, br_v3_add(br_v3_scale(u, -hu), br_v3_scale(v, hv)));
    poly[2].p = br_v3_add(ic, br_v3_add(br_v3_scale(u, -hu), br_v3_scale(v, -hv)));
    poly[3].p = br_v3_add(ic, br_v3_add(br_v3_scale(u, hu), br_v3_scale(v, -hv)));
    for (i = 0; i < 4; ++i) poly[i].id = (uint32_t)i;
    n = 4;
    a1 = (axis + 1) % 3; a2 = (axis + 2) % 3;
    for (side = 0; side < 4 && n > 0; ++side) {               /* the reference face's four sides */
        int ax = side < 2 ? a1 : a2;
        float sg = (side & 1) ? -1.0f : 1.0f;
        br_vec3 pn = br_v3_scale(ref->ax[ax], sg);
        float off = br_v3_dot(ref->x, pn) + v3c(ref->half, ax);
        n = clip_plane(poly, n, pn, off, tmp, (uint32_t)side);
        memcpy(poly, tmp, sizeof(cpt) * (size_t)n);
    }
    rc = br_v3_add(ref->x, br_v3_scale(nref, v3c(ref->half, axis)));
    for (i = 0; i < n; ++i) {                                 /* the points below the reference face */
        float sep = br_v3_dot(nref, br_v3_sub(poly[i].p, rc));
        depth[i] = -sep;
        if (sep <= P3_SLOP) {
            poly[count] = poly[i];
            depth[count] = -sep;
            ++count;
        }
    }
    if (count == 0) return 0;
    if (count <= 4) {
        for (i = 0; i < count; ++i) pick[i] = i;
        chosen = count;
    } else {                                                  /* four that spread out: the deepest, the farthest from it, ... */
        float far1 = -1, ar = 0;
        int p0 = 0, p1 = 0, p2 = -1, p3 = -1;
        for (i = 0; i < count; ++i) if (depth[i] > deepest) { deepest = depth[i]; p0 = i; }
        for (i = 0; i < count; ++i) {
            float d = br_v3_len2(br_v3_sub(poly[i].p, poly[p0].p));
            if (d > far1) { far1 = d; p1 = i; }
        }
        {
            float hi = 0, lo = 0;
            for (i = 0; i < count; ++i) {
                ar = br_v3_dot(br_v3_cross(br_v3_sub(poly[p1].p, poly[p0].p), br_v3_sub(poly[i].p, poly[p0].p)), nref);
                if (ar > hi) { hi = ar; p2 = i; }
                if (ar < lo) { lo = ar; p3 = i; }
            }
        }
        pick[chosen++] = p0;
        if (p1 != p0) pick[chosen++] = p1;
        if (p2 >= 0) pick[chosen++] = p2;
        if (p3 >= 0) pick[chosen++] = p3;
    }
    for (i = 0; i < chosen; ++i) {
        const cpt *pt = &poly[pick[i]];
        c[i].p = br_v3_add(pt->p, br_v3_scale(nref, depth[pick[i]] * 0.5f));   /* half way between the faces */
        c[i].depth = depth[pick[i]] > 0 ? depth[pick[i]] : 0;
        c[i].id = idbase | ((uint32_t)kk << 8) | pt->id;
    }
    return chosen;
}

static int box_box(const p3body *a, const p3body *b, br_vec3 *nout, p3contact *c) {
    br_vec3 d = br_v3_sub(b->x, a->x), best_n = br_v3(0, 1, 0);
    float best = 1e30f, face_best;
    int best_axis = -1, i;                                 /* 0-2 a's faces, 3-5 b's, 6+ edges */
    for (i = 0; i < 15; ++i) {
        br_vec3 L;
        float ra = 0, rb = 0, dist, ov;
        int k;
        if (i < 3) L = a->ax[i];
        else if (i < 6) L = b->ax[i - 3];
        else {
            float len;
            L = br_v3_cross(a->ax[(i - 6) / 3], b->ax[(i - 6) % 3]);
            len = br_v3_len(L);
            if (len < 1e-4f) continue;
            L = br_v3_scale(L, 1.0f / len);
        }
        for (k = 0; k < 3; ++k) {
            ra += v3c(a->half, k) * fabsf(br_v3_dot(a->ax[k], L));
            rb += v3c(b->half, k) * fabsf(br_v3_dot(b->ax[k], L));
        }
        dist = br_v3_dot(d, L);
        ov = ra + rb - fabsf(dist);
        if (ov < 0) return 0;
        if (i >= 6 && !(ov < best * 0.95f - 0.002f)) continue;    /* (faces are steadier: prefer them) */
        if (i < 6 && i >= 3 && !(ov < best * 0.98f - 0.0005f)) continue;
        if (ov < best) {
            best = ov;
            best_axis = i;
            best_n = dist < 0 ? br_v3_neg(L) : L;
        }
    }
    face_best = best;
    BR_UNUSED(face_best);
    if (best_axis < 0) return 0;
    *nout = best_n;
    if (best_axis < 3) return face_contacts(a, best_axis, best_n, b, c, 0x0000u | ((uint32_t)best_axis << 12));
    if (best_axis < 6) return face_contacts(b, best_axis - 3, br_v3_neg(best_n), a, c, 0x4000u | ((uint32_t)(best_axis - 3) << 12));
    {                                                          /* edge against edge */
        int ia = (best_axis - 6) / 3, ib = (best_axis - 6) % 3, k;
        br_vec3 pa = a->x, pb = b->x, da = a->ax[ia], db = b->ax[ib], r;
        float aa, bb, ab, ad, bd, den, s, t;
        for (k = 0; k < 3; ++k) {
            if (k != ia) pa = br_v3_add(pa, br_v3_scale(a->ax[k], v3c(a->half, k) * (br_v3_dot(a->ax[k], best_n) > 0 ? 1.0f : -1.0f)));
            if (k != ib) pb = br_v3_add(pb, br_v3_scale(b->ax[k], v3c(b->half, k) * (br_v3_dot(b->ax[k], best_n) > 0 ? -1.0f : 1.0f)));
        }
        r = br_v3_sub(pa, pb);
        aa = br_v3_dot(da, da); bb = br_v3_dot(db, db); ab = br_v3_dot(da, db);
        ad = br_v3_dot(da, r); bd = br_v3_dot(db, r);
        den = aa * bb - ab * ab;
        s = fabsf(den) > 1e-9f ? (ab * bd - bb * ad) / den : 0;
        t = fabsf(den) > 1e-9f ? (aa * bd - ab * ad) / den : 0;
        s = br_clampf(s, -v3c(a->half, ia), v3c(a->half, ia));
        t = br_clampf(t, -v3c(b->half, ib), v3c(b->half, ib));
        c[0].p = br_v3_scale(br_v3_add(br_v3_add(pa, br_v3_scale(da, s)), br_v3_add(pb, br_v3_scale(db, t))), 0.5f);
        c[0].depth = best;
        c[0].id = 0x8000u | ((uint32_t)ia << 2) | (uint32_t)ib;
        return 1;
    }
}


/* ---- capsules and meshes --------------------------------------------------------------------- */

static br_vec3 seg_closest(br_vec3 a, br_vec3 b, br_vec3 p) {
    br_vec3 ab = br_v3_sub(b, a);
    float l2 = br_v3_dot(ab, ab), t = l2 > 1e-12f ? br_clampf(br_v3_dot(br_v3_sub(p, a), ab) / l2, 0, 1) : 0;
    return br_v3_add_scaled(a, ab, t);
}

/* The closest points of two segments (Ericson, Real-Time Collision Detection 5.1.9). */
static void seg_seg(br_vec3 p1, br_vec3 q1, br_vec3 p2, br_vec3 q2, br_vec3 *c1, br_vec3 *c2) {
    br_vec3 d1 = br_v3_sub(q1, p1), d2 = br_v3_sub(q2, p2), r = br_v3_sub(p1, p2);
    float a = br_v3_dot(d1, d1), e = br_v3_dot(d2, d2), f = br_v3_dot(d2, r), s = 0, t = 0;
    if (a <= 1e-12f && e <= 1e-12f) { *c1 = p1; *c2 = p2; return; }
    if (a <= 1e-12f) t = br_clampf(f / e, 0, 1);
    else {
        float c = br_v3_dot(d1, r);
        if (e <= 1e-12f) s = br_clampf(-c / a, 0, 1);
        else {
            float b = br_v3_dot(d1, d2), denom = a * e - b * b;
            s = denom > 1e-12f ? br_clampf((b * f - c * e) / denom, 0, 1) : 0;
            t = (b * s + f) / e;
            if (t < 0) { t = 0; s = br_clampf(-c / a, 0, 1); }
            else if (t > 1) { t = 1; s = br_clampf((b - c) / a, 0, 1); }
        }
    }
    *c1 = br_v3_add_scaled(p1, d1, s);
    *c2 = br_v3_add_scaled(p2, d2, t);
}

/* The closest point of triangle abc to p (Ericson 5.1.5). */
static br_vec3 tri_closest(br_vec3 p, br_vec3 a, br_vec3 b, br_vec3 c) {
    br_vec3 ab = br_v3_sub(b, a), ac = br_v3_sub(c, a), ap = br_v3_sub(p, a), bp, cp;
    float d1 = br_v3_dot(ab, ap), d2 = br_v3_dot(ac, ap), d3, d4, d5, d6, va, vb, vc, v, w;
    if (d1 <= 0 && d2 <= 0) return a;
    bp = br_v3_sub(p, b);
    d3 = br_v3_dot(ab, bp); d4 = br_v3_dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return br_v3_add_scaled(a, ab, d1 / (d1 - d3));
    cp = br_v3_sub(p, c);
    d5 = br_v3_dot(ab, cp); d6 = br_v3_dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return br_v3_add_scaled(a, ac, d2 / (d2 - d6));
    va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return br_v3_add_scaled(b, br_v3_sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6)));
    v = vb / (va + vb + vc);
    w = vc / (va + vb + vc);
    return br_v3_add(a, br_v3_add(br_v3_scale(ab, v), br_v3_scale(ac, w)));
}

static p3body p3_sphere_at(br_vec3 x, float r) {
    p3body b;
    memset(&b, 0, sizeof(b));
    b.shape = BR_COLLIDER3D_SPHERE;
    b.x = x;
    b.radius = r;
    b.q = br_quat_identity();
    p3_shape(&b);
    return b;
}

/* Keeps the deepest `max` of n contacts (each with its own normal), and gives them one normal: the deepest's. */
typedef struct p3cand { p3contact c; br_vec3 n; } p3cand;
static int p3_best(p3cand *cand, int n, br_vec3 *nout, p3contact *out) {
    int i, j, k = n < P3_MAX_CONTACTS ? n : P3_MAX_CONTACTS;
    for (i = 1; i < n; ++i) {                                  /* deepest first */
        p3cand x = cand[i];
        for (j = i; j > 0 && cand[j - 1].c.depth < x.c.depth; --j) cand[j] = cand[j - 1];
        cand[j] = x;
    }
    if (k == 0) return 0;
    *nout = cand[0].n;
    for (i = 0; i < k; ++i) {
        out[i] = cand[i].c;
        out[i].depth = br_maxf(0, cand[i].c.depth * br_v3_dot(cand[i].n, cand[0].n));
    }
    return k;
}

static int box_sphere(const p3body *a, const p3body *b, br_vec3 *n, p3contact *c);
static int sphere_sphere(const p3body *a, const p3body *b, br_vec3 *n, p3contact *c);

/* capsule a against a sphere, capsule or box b: spheres along a's segment. Normal from a to b. */
static int capsule_vs(const p3body *a, const p3body *b, br_vec3 *n, p3contact *c) {
    if (b->shape == BR_COLLIDER3D_SPHERE) {
        p3body s = p3_sphere_at(seg_closest(a->seg_a, a->seg_b, b->x), a->radius);
        return sphere_sphere(&s, b, n, c);
    }
    if (b->shape == BR_COLLIDER3D_CAPSULE) {
        br_vec3 p, q;
        p3body s1, s2;
        seg_seg(a->seg_a, a->seg_b, b->seg_a, b->seg_b, &p, &q);
        s1 = p3_sphere_at(p, a->radius);
        s2 = p3_sphere_at(q, b->radius);
        return sphere_sphere(&s1, &s2, n, c);
    }
    {   /* a box: both ends, and the point of the segment nearest the box */
        br_vec3 pts[3], p = seg_closest(a->seg_a, a->seg_b, b->x);
        p3cand cand[3];
        int k, m = 0, i;
        memset(cand, 0, sizeof(cand));
        for (i = 0; i < 2; ++i) {                              /* (the box's nearest point to p, and back) */
            br_vec3 d = br_v3_sub(p, b->x), q = b->x;
            int ax;
            for (ax = 0; ax < 3; ++ax) q = br_v3_add_scaled(q, b->ax[ax], br_clampf(br_v3_dot(d, b->ax[ax]), -v3c(b->half, ax), v3c(b->half, ax)));
            p = seg_closest(a->seg_a, a->seg_b, q);
        }
        pts[0] = a->seg_a; pts[1] = a->seg_b; pts[2] = p;
        for (k = 0; k < 3; ++k) {
            p3body s = p3_sphere_at(pts[k], a->radius);
            br_vec3 nn;
            if (k == 2 && (br_v3_dist(p, a->seg_a) < 1e-3f || br_v3_dist(p, a->seg_b) < 1e-3f)) continue;
            if (box_sphere(b, &s, &nn, &cand[m].c) > 0) {
                cand[m].n = br_v3_neg(nn);
                cand[m].c.id = (uint32_t)k;
                ++m;
            }
        }
        return p3_best(cand, m, n, c);
    }
}

/* mesh a (its triangles in the world) against a sphere, capsule or box b. Normal from a to b. */
#define P3_MESH_CANDS 64
static int cmp_u32(const void *x, const void *y) {
    uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    return a < b ? -1 : a > b;
}

#define P3_NEAR_TRIS 512
static int mesh_vs(const p3body *a, const p3body *b, br_vec3 *n, p3contact *c) {
    const br_mesh *m = a->mesh;
    p3cand cand[P3_MESH_CANDS];
    uint32_t near[P3_NEAR_TRIS], near_count = 0, k_near, i;
    bool use_near = false;
    int count = 0;
    if (!m || !a->mv) return 0;
    memset(cand, 0, sizeof(cand));
    if (m->index_count / 3 > 64) {                              /* a big mesh: only the triangles near b (its BVH) */
        br_vec3 lo, hi;
        int k;
        for (k = 0; k < 8; ++k) {
            br_vec3 p = br_m4_mul_point(a->to_local, br_v3(k & 1 ? b->hi.x : b->lo.x, k & 2 ? b->hi.y : b->lo.y, k & 4 ? b->hi.z : b->lo.z));
            lo = k ? br_v3_min(lo, p) : p;
            hi = k ? br_v3_max(hi, p) : p;
        }
        near_count = br_mesh_triangles_in(m, lo, hi, near, P3_NEAR_TRIS);
        if (near_count <= P3_NEAR_TRIS) {
            use_near = true;
            qsort(near, near_count, sizeof(uint32_t), cmp_u32);   /* (in order, as without it: the same contacts) */
        }
    }
    for (k_near = 0, i = 0; use_near ? k_near < near_count : i + 2 < m->index_count; use_near ? ++k_near : (i += 3)) {
        br_vec3 p0, p1, p2, lo, hi, tn;
        float tl;
        int k;
        if (use_near) i = near[k_near] * 3;
        p0 = a->mv[m->indices[i]]; p1 = a->mv[m->indices[i + 1]]; p2 = a->mv[m->indices[i + 2]];
        lo = br_v3_min(p0, br_v3_min(p1, p2));
        hi = br_v3_max(p0, br_v3_max(p1, p2));
        if (lo.x > b->hi.x || hi.x < b->lo.x || lo.y > b->hi.y || hi.y < b->lo.y || lo.z > b->hi.z || hi.z < b->lo.z) continue;
        tn = br_v3_cross(br_v3_sub(p1, p0), br_v3_sub(p2, p0));
        tl = br_v3_len(tn);
        if (tl < 1e-12f) continue;
        tn = br_v3_scale(tn, 1.0f / tl);
        if (b->shape == BR_COLLIDER3D_BOX) {                    /* the box's corners that went under the triangle */
            for (k = 0; k < 8 && count < P3_MESH_CANDS; ++k) {
                br_vec3 corner = br_v3_add(b->x, br_v3_add(br_v3_add(br_v3_scale(b->ax[0], (k & 1 ? 1.0f : -1.0f) * b->half.x),
                                                                     br_v3_scale(b->ax[1], (k & 2 ? 1.0f : -1.0f) * b->half.y)),
                                                           br_v3_scale(b->ax[2], (k & 4 ? 1.0f : -1.0f) * b->half.z)));
                float dist = br_v3_dot(br_v3_sub(corner, p0), tn), reach = 2 * BR_MAX(b->half.x, BR_MAX(b->half.y, b->half.z));
                br_vec3 q;
                if (dist >= 0 || dist < -reach) continue;
                q = br_v3_add_scaled(corner, tn, -dist);             /* on the triangle's plane: inside it? */
                if (br_v3_dist(tri_closest(q, p0, p1, p2), q) > 1e-4f) continue;
                cand[count].n = tn;
                cand[count].c.p = corner;
                cand[count].c.depth = -dist;
                cand[count].c.id = (i / 3) * 8 + (uint32_t)k;
                ++count;
            }
        } else {                                                /* a sphere, or spheres along a capsule */
            br_vec3 pts[3];
            int np = 1;
            float r = b->radius;
            if (b->shape == BR_COLLIDER3D_CAPSULE) {
                br_vec3 p = seg_closest(b->seg_a, b->seg_b, br_v3_scale(br_v3_add(p0, br_v3_add(p1, p2)), 1.0f / 3));
                int it;
                for (it = 0; it < 2; ++it) p = seg_closest(b->seg_a, b->seg_b, tri_closest(p, p0, p1, p2));
                pts[0] = p; pts[1] = b->seg_a; pts[2] = b->seg_b;
                np = 3;
            } else {
                pts[0] = b->x;
            }
            for (k = 0; k < np && count < P3_MESH_CANDS; ++k) {
                br_vec3 q = tri_closest(pts[k], p0, p1, p2), d = br_v3_sub(pts[k], q);
                float dist = br_v3_len(d), side = br_v3_dot(br_v3_sub(pts[k], p0), tn);
                if (dist >= r) continue;
                if (side < 0 && dist < 1e-6f) continue;
                if (side < -1e-6f) {                             /* behind the face (straight behind it, not off past an edge): */
                    br_vec3 on = br_v3_add_scaled(pts[k], tn, -side);
                    if (br_v3_dist(tri_closest(on, p0, p1, p2), on) > 1e-4f) side = 0;   /* (past an edge: touching that edge) */
                }
                if (side < -1e-6f) {                             /* ... pushed out the front way */
                    cand[count].n = tn;
                    cand[count].c.depth = r + dist;
                } else {
                    cand[count].n = dist > 1e-6f ? br_v3_scale(d, 1.0f / dist) : tn;
                    cand[count].c.depth = r - dist;
                }
                cand[count].c.p = q;
                cand[count].c.id = (i / 3) * 4 + (uint32_t)k;
                ++count;
            }
        }
    }
    if (b->shape == BR_COLLIDER3D_SPHERE && count > 1) count = p3_best(cand, count, n, c) > 0 ? 1 : 0;   /* (a sphere: its one deepest touch) */
    else return p3_best(cand, count, n, c);
    return count;
}

static int p3_collide(const p3body *a, const p3body *b, br_vec3 *n, p3contact *c) {
    if (a->shape == BR_COLLIDER3D_MESH || b->shape == BR_COLLIDER3D_MESH) {
        int k;
        if (a->shape == BR_COLLIDER3D_MESH && b->shape == BR_COLLIDER3D_MESH) return 0;   /* (scenery doesn't meet scenery) */
        if (a->shape == BR_COLLIDER3D_MESH) return mesh_vs(a, b, n, c);
        k = mesh_vs(b, a, n, c);
        *n = br_v3_neg(*n);
        return k;
    }
    if (a->shape == BR_COLLIDER3D_CAPSULE) return capsule_vs(a, b, n, c);
    if (b->shape == BR_COLLIDER3D_CAPSULE) {
        int k = capsule_vs(b, a, n, c);
        *n = br_v3_neg(*n);
        return k;
    }
    if (a->shape == BR_COLLIDER3D_SPHERE && b->shape == BR_COLLIDER3D_SPHERE) return sphere_sphere(a, b, n, c);
    if (a->shape == BR_COLLIDER3D_BOX && b->shape == BR_COLLIDER3D_SPHERE) return box_sphere(a, b, n, c);
    if (a->shape == BR_COLLIDER3D_SPHERE && b->shape == BR_COLLIDER3D_BOX) {
        int k = box_sphere(b, a, n, c);
        *n = br_v3_neg(*n);
        return k;
    }
    return box_box(a, b, n, c);
}

/* ---- pairs and the solver ----------------------------------------------------------------- */

static bool p3_layers(const p3body *a, const p3body *b) {
    return (a->mask & (1 << (b->layer & 31))) && (b->mask & (1 << (a->layer & 31)));
}

static const p3arb *p3_previous(const p3state *s, br_entity ea, br_entity eb) {
    uint32_t i;
    int prev = 1 - s->cur;
    for (i = 0; i < s->arb_count[prev]; ++i)
        if (s->arb[prev][i].ea == ea && s->arb[prev][i].eb == eb) return &s->arb[prev][i];
    return NULL;
}

static void p3_basis(br_vec3 n, br_vec3 *t1, br_vec3 *t2) {
    *t1 = br_v3_norm(br_v3_cross(n, fabsf(n.x) < 0.57f ? br_v3(1, 0, 0) : br_v3(0, 1, 0)));
    *t2 = br_v3_cross(n, *t1);
}

static br_vec3 p3_vel_at(const p3body *b, br_vec3 r) { return br_v3_add(b->v, br_v3_cross(b->w, r)); }

static void p3_find_pairs(p3state *s) {
    uint32_t i, j, n = s->body_count;
    s->arb_count[s->cur] = 0;
    if (!grow((void **)&s->order, &s->order_cap, n + 1, sizeof(uint32_t))) return;
    for (i = 0; i < n; ++i) s->order[i] = i;
    for (i = 1; i < n; ++i) {                                 /* insertion sort on lo.x: nearly sorted step to step */
        uint32_t x = s->order[i];
        float key = s->bodies[x].lo.x;
        for (j = i; j > 0 && s->bodies[s->order[j - 1]].lo.x > key; --j) s->order[j] = s->order[j - 1];
        s->order[j] = x;
    }
    for (i = 0; i < n; ++i) {
        p3body *A = &s->bodies[s->order[i]];
        if (!A->has_collider) continue;
        for (j = i + 1; j < n; ++j) {
            p3body *B = &s->bodies[s->order[j]], *a, *b;
            p3arb *arb;
            br_vec3 nrm;
            p3contact c[P3_MAX_CONTACTS];
            int count, k;
            uint32_t ia, ib;
            if (B->lo.x > A->hi.x) break;
            if (!B->has_collider) continue;
            if (B->lo.y > A->hi.y || B->hi.y < A->lo.y || B->lo.z > A->hi.z || B->hi.z < A->lo.z) continue;
            if (A->type == BR_BODY_STATIC && B->type == BR_BODY_STATIC) continue;
            if (!A->trigger && !B->trigger && A->type != BR_BODY_DYNAMIC && B->type != BR_BODY_DYNAMIC) continue;
            if (!p3_layers(A, B)) continue;
            ia = s->order[i]; ib = s->order[j];
            if (A->e > B->e) { uint32_t t = ia; ia = ib; ib = t; }   /* (always the same way round) */
            a = &s->bodies[ia]; b = &s->bodies[ib];
            memset(c, 0, sizeof(c));
            count = p3_collide(a, b, &nrm, c);
            if (count <= 0) continue;
            if (!grow((void **)&s->arb[s->cur], &s->arb_cap[s->cur], s->arb_count[s->cur] + 1, sizeof(p3arb))) return;
            arb = &s->arb[s->cur][s->arb_count[s->cur]++];
            memset(arb, 0, sizeof(*arb));
            arb->ea = a->e; arb->eb = b->e; arb->a = ia; arb->b = ib;
            arb->n = nrm;
            p3_basis(nrm, &arb->t1, &arb->t2);
            arb->count = count;
            for (k = 0; k < count; ++k) arb->c[k] = c[k];
            arb->trigger = a->trigger || b->trigger;
            arb->friction = sqrtf(br_maxf(a->friction, 0) * br_maxf(b->friction, 0));
            arb->bounce = br_maxf(a->bounce, b->bounce);
        }
    }
}

static void p3_report(p3state *s) {
    uint32_t i, k;
    int prev = 1 - s->cur;
    s->event_count = 0;
    for (i = 0; i < s->arb_count[s->cur]; ++i) {               /* new, and warm starts for the old */
        p3arb *a = &s->arb[s->cur][i];
        const p3arb *old = p3_previous(s, a->ea, a->eb);
        const p3body *A = &s->bodies[a->a], *B = &s->bodies[a->b];
        if (!old) {
            float speed = -br_v3_dot(br_v3_sub(p3_vel_at(B, br_v3_sub(a->c[0].p, B->x)), p3_vel_at(A, br_v3_sub(a->c[0].p, A->x))), a->n);
            a->speed = speed > 0 ? speed : 0;
            p3_event(s, a->trigger ? BR_TRIGGER_ENTER : BR_CONTACT_BEGIN, a->ea, a->eb, a->n, a->c[0].p, a->speed);
            continue;
        }
        if (old->trigger != a->trigger) continue;
        for (k = 0; k < (uint32_t)a->count; ++k) {
            int m;
            for (m = 0; m < old->count; ++m)
                if (old->c[m].id == a->c[k].id) {
                    a->c[k].pn = old->c[m].pn; a->c[k].pt1 = old->c[m].pt1; a->c[k].pt2 = old->c[m].pt2;
                    break;
                }
        }
    }
    for (i = 0; i < s->arb_count[prev]; ++i) {                 /* pairs that came apart */
        const p3arb *o = &s->arb[prev][i];
        bool still = false;
        uint32_t j;
        for (j = 0; j < s->arb_count[s->cur] && !still; ++j) still = s->arb[s->cur][j].ea == o->ea && s->arb[s->cur][j].eb == o->eb;
        if (!still) p3_event(s, o->trigger ? BR_TRIGGER_EXIT : BR_CONTACT_END, o->ea, o->eb, o->n, br_v3(0, 0, 0), 0);
    }
}

static float p3_k(const p3body *a, const p3body *b, br_vec3 ra, br_vec3 rb, br_vec3 n) {
    br_vec3 xa = br_v3_cross(m3_mul(&a->inv_i, br_v3_cross(ra, n)), ra);
    br_vec3 xb = br_v3_cross(m3_mul(&b->inv_i, br_v3_cross(rb, n)), rb);
    return a->inv_mass + b->inv_mass + br_v3_dot(br_v3_add(xa, xb), n);
}

static void p3_impulse(p3body *a, p3body *b, br_vec3 ra, br_vec3 rb, br_vec3 P) {
    a->v = br_v3_sub(a->v, br_v3_scale(P, a->inv_mass));
    a->w = br_v3_sub(a->w, m3_mul(&a->inv_i, br_v3_cross(ra, P)));
    b->v = br_v3_add(b->v, br_v3_scale(P, b->inv_mass));
    b->w = br_v3_add(b->w, m3_mul(&b->inv_i, br_v3_cross(rb, P)));
}

static void p3_pre(p3state *s, p3arb *arb, float inv_dt) {
    p3body *a = &s->bodies[arb->a], *b = &s->bodies[arb->b];
    int k;
    for (k = 0; k < arb->count; ++k) {
        p3contact *c = &arb->c[k];
        float kn, vn;
        c->ra = br_v3_sub(c->p, a->x);
        c->rb = br_v3_sub(c->p, b->x);
        kn = p3_k(a, b, c->ra, c->rb, arb->n);
        c->mn = kn > 1e-12f ? 1.0f / kn : 0;
        kn = p3_k(a, b, c->ra, c->rb, arb->t1);
        c->mt1 = kn > 1e-12f ? 1.0f / kn : 0;
        kn = p3_k(a, b, c->ra, c->rb, arb->t2);
        c->mt2 = kn > 1e-12f ? 1.0f / kn : 0;
        c->bias = P3_BAUMGARTE * inv_dt * br_maxf(0, c->depth - P3_SLOP);
        vn = br_v3_dot(br_v3_sub(p3_vel_at(b, c->rb), p3_vel_at(a, c->ra)), arb->n);
        if (vn < -P3_BOUNCE_SPEED && arb->bounce > 0) c->bias = br_maxf(c->bias, -arb->bounce * vn);
        p3_impulse(a, b, c->ra, c->rb, br_v3_add(br_v3_add(br_v3_scale(arb->n, c->pn), br_v3_scale(arb->t1, c->pt1)), br_v3_scale(arb->t2, c->pt2)));
    }
}

static void p3_solve(p3state *s, p3arb *arb) {
    p3body *a = &s->bodies[arb->a], *b = &s->bodies[arb->b];
    int k;
    for (k = 0; k < arb->count; ++k) {
        p3contact *c = &arb->c[k];
        br_vec3 dv = br_v3_sub(p3_vel_at(b, c->rb), p3_vel_at(a, c->ra));
        float vn = br_v3_dot(dv, arb->n), dp = c->mn * (-vn + c->bias), old = c->pn, lim, o1, o2;
        c->pn = br_maxf(old + dp, 0);
        p3_impulse(a, b, c->ra, c->rb, br_v3_scale(arb->n, c->pn - old));
        lim = arb->friction * c->pn;                          /* friction, inside the Coulomb pyramid */
        dv = br_v3_sub(p3_vel_at(b, c->rb), p3_vel_at(a, c->ra));
        o1 = c->pt1;
        c->pt1 = br_clampf(o1 - c->mt1 * br_v3_dot(dv, arb->t1), -lim, lim);
        o2 = c->pt2;
        c->pt2 = br_clampf(o2 - c->mt2 * br_v3_dot(dv, arb->t2), -lim, lim);
        p3_impulse(a, b, c->ra, c->rb, br_v3_add(br_v3_scale(arb->t1, c->pt1 - o1), br_v3_scale(arb->t2, c->pt2 - o2)));
    }
}

/* ---- the step ------------------------------------------------------------------------------- */

br_vec3 br_physics3d_gravity(const br_world *w) {
    p3_ids id;
    if (p3_ids_of(w, &id) && w->pools[id.world].count > 0) return ((const br_physics_world3d *)w->pools[id.world].data)->gravity;
    return k_world3_default.gravity;
}

void br_physics3d_step(br_world *w, float dt) {
    p3_ids id;
    p3state *s;
    br_vec3 g;
    int iterations = P3_DEFAULT_ITERATIONS, it;
    uint32_t i, d;
    float inv_dt;
    if (dt <= 0 || !p3_ids_of(w, &id)) return;
    if (br_component_count(w, id.col) == 0 && br_component_count(w, id.body) == 0) return;
    if (!(s = p3_state(w))) return;
    g = br_physics3d_gravity(w);
    if (w->pools[id.world].count > 0) {
        int n = ((const br_physics_world3d *)w->pools[id.world].data)->iterations;
        iterations = n > 0 ? n : 1;
    }
    inv_dt = 1.0f / dt;
    p3_gather(w, &id, s);
    for (i = 0; i < s->body_count; ++i) {                     /* forces -> velocities */
        p3body *b = &s->bodies[i];
        if (b->type != BR_BODY_DYNAMIC) continue;
        b->v = br_v3_add(b->v, br_v3_scale(br_v3_add(br_v3_scale(g, b->rb->gravity_scale), br_v3_scale(b->rb->force, b->inv_mass)), dt));
        b->w = br_v3_add(b->w, br_v3_scale(m3_mul(&b->inv_i, b->rb->torque), dt));
        b->v = br_v3_scale(b->v, 1.0f / (1.0f + dt * b->rb->linear_damping));
        b->w = br_v3_scale(b->w, 1.0f / (1.0f + dt * b->rb->angular_damping));
    }
    s->cur = 1 - s->cur;
    p3_find_pairs(s);
    p3_report(s);
    for (i = 0; i < s->arb_count[s->cur]; ++i)
        if (!s->arb[s->cur][i].trigger) p3_pre(s, &s->arb[s->cur][i], inv_dt);
    for (it = 0; it < iterations; ++it)
        for (i = 0; i < s->arb_count[s->cur]; ++i)
            if (!s->arb[s->cur][i].trigger) p3_solve(s, &s->arb[s->cur][i]);
    for (d = 0; d <= s->max_depth; ++d)                       /* velocities -> positions, parents first */
    for (i = 0; i < s->body_count; ++i) {
        p3body *b = &s->bodies[i];
        if (!b->rb || b->depth != d) continue;
        if (b->type != BR_BODY_STATIC) {
            br_quat dq, q = b->q;
            br_vec3 pos;
            float wl = br_v3_len(b->w);
            if (wl > 100.0f) b->w = br_v3_scale(b->w, 100.0f / wl);   /* (no wild spinning) */
            b->x = br_v3_add(b->x, br_v3_scale(b->v, dt));
            dq.x = b->w.x * dt * 0.5f; dq.y = b->w.y * dt * 0.5f; dq.z = b->w.z * dt * 0.5f; dq.w = 0;
            dq = br_quat_mul(dq, q);
            q.x += dq.x; q.y += dq.y; q.z += dq.z; q.w += dq.w;
            b->q = br_quat_norm(q);
            pos = br_v3_sub(b->x, br_quat_rotate(b->q, b->offset_local));
            if (b->parented) br_set_world_matrix3d(w, b->e, br_m4_trs(pos, b->q, b->scale));
            else {
                b->t->position = pos;
                if (wl > 0 || b->type == BR_BODY_KINEMATIC) b->t->rotation = br_quat_to_euler(b->q);
            }
            b->rb->velocity = b->v;
            b->rb->angular_velocity = br_v3_scale(b->w, BR_RAD2DEG);
        }
        b->rb->force = br_v3(0, 0, 0);
        b->rb->torque = br_v3(0, 0, 0);
    }
}

const br_contact3d_event *br_physics3d_events(const br_world *w, uint32_t *count) {
    const p3state *s = (const p3state *)br_world_attached(w, &p3_state_key);
    *count = s ? s->event_count : 0;
    return s ? s->events : NULL;
}

/* ---- pushing, asking -------------------------------------------------------------------------- */

static br_rigidbody3d *p3_dynamic(br_world *w, br_entity e) {
    p3_ids id;
    br_rigidbody3d *rb;
    if (!p3_ids_of(w, &id) || !(rb = (br_rigidbody3d *)br_get(w, e, id.body)) || rb->type != BR_BODY_DYNAMIC) return NULL;
    return rb;
}

static br_vec3 p3_centre(const br_world *w, br_entity e) {
    p3_ids id;
    const br_collider3d *c;
    br_vec3 p, s;
    br_quat q;
    br_world_transform3d(w, e, &p, &q, &s);
    if (p3_ids_of(w, &id) && (c = (const br_collider3d *)br_get(w, e, id.col)) != NULL) p = br_v3_add(p, br_quat_rotate(q, br_v3_mul(c->offset, s)));
    return p;
}

void br_physics3d_apply_force(br_world *w, br_entity e, br_vec3 force, const br_vec3 *point) {
    br_rigidbody3d *rb = p3_dynamic(w, e);
    if (!rb) return;
    rb->force = br_v3_add(rb->force, force);
    if (point) rb->torque = br_v3_add(rb->torque, br_v3_cross(br_v3_sub(*point, p3_centre(w, e)), force));
}

void br_physics3d_apply_torque(br_world *w, br_entity e, br_vec3 torque) {
    br_rigidbody3d *rb = p3_dynamic(w, e);
    if (rb) rb->torque = br_v3_add(rb->torque, torque);
}

void br_physics3d_apply_impulse(br_world *w, br_entity e, br_vec3 impulse, const br_vec3 *point) {
    br_rigidbody3d *rb = p3_dynamic(w, e);
    float m;
    if (!rb) return;
    m = rb->mass > 0.001f ? rb->mass : 0.001f;
    rb->velocity = br_v3_add(rb->velocity, br_v3_scale(impulse, 1.0f / m));
    if (point && !rb->lock_rotation) {                         /* a spin: through this step's inertia */
        p3state tmp;
        p3_ids id;
        memset(&tmp, 0, sizeof(tmp));
        if (p3_ids_of(w, &id) && br_has(w, e, id.col)) {
            br_vec3 r = br_v3_sub(*point, p3_centre(w, e)), dw;
            const p3body *b;
            p3_gather(w, &id, &tmp);
            for (b = tmp.bodies; b && b < tmp.bodies + tmp.body_count; ++b)
                if (b->e == e) {
                    dw = m3_mul(&b->inv_i, br_v3_cross(r, impulse));
                    rb->angular_velocity = br_v3_add(rb->angular_velocity, br_v3_scale(dw, BR_RAD2DEG));
                    break;
                }
            br_free(tmp.bodies);
        }
    }
}

/* One collider, set up as the step would: a body on its own. */
static bool p3_one(const br_world *w, const p3_ids *id, br_entity e, p3body *b) {
    const br_collider3d *c = (const br_collider3d *)br_get(w, e, id->col);
    br_vec3 pos, sc, absc;
    br_quat q;
    if (!c || !br_has(w, e, id->t3)) return false;
    memset(b, 0, sizeof(*b));
    b->e = e;
    br_world_transform3d(w, e, &pos, &q, &sc);
    b->q = br_quat_norm(q);
    absc = br_v3(fabsf(sc.x), fabsf(sc.y), fabsf(sc.z));
    b->shape = c->shape;
    b->half = br_v3_scale(br_v3_mul(br_v3(fabsf(c->size.x), fabsf(c->size.y), fabsf(c->size.z)), absc), 0.5f);
    b->radius = fabsf(c->radius) * BR_MAX(absc.x, BR_MAX(absc.y, absc.z));
    b->x = br_v3_add(pos, br_quat_rotate(b->q, br_v3_mul(c->offset, sc)));
    b->trigger = c->trigger;
    b->layer = c->layer;
    b->mask = c->mask;
    b->friction = c->friction;
    b->bounce = c->bounce;
    if (b->shape == BR_COLLIDER3D_CAPSULE) b->radius = fabsf(c->radius) * BR_MAX(absc.x, absc.z);
    if (b->shape == BR_COLLIDER3D_MESH && (b->mesh = p3_mesh_of(w, e)) != NULL) {
        br_vec3 *v = (br_vec3 *)br_alloc(sizeof(br_vec3) * (b->mesh->vert_count ? b->mesh->vert_count : 1));
        if (!v) return false;
        br_mat4 world = br_world_matrix3d(w, e);
        p3_mesh_world(b->mesh, world, v);
        b->to_local = br_m4_inverse(world);
        b->mv = v;
        b->owns_mv = true;
    }
    p3_shape(b);
    return true;
}

static void p3_done(p3body *b) {
    if (b->owns_mv) br_free((void *)b->mv);
    b->mv = NULL;
    b->owns_mv = false;
}

/* Where a ray (from + d * t, |d| = 1, t up to len) first meets a sphere; -1 if it doesn't. */
static float ray_sphere(br_vec3 from, br_vec3 d, br_vec3 centre, float r) {
    br_vec3 m = br_v3_sub(from, centre);
    float bb = br_v3_dot(m, d), cc = br_v3_dot(m, m) - r * r, disc = bb * bb - cc, t;
    if (cc > 0 && bb > 0) return -1;
    if (disc < 0) return -1;
    t = -bb - sqrtf(disc);
    return t < 0 ? 0 : t;
}

/* ... a capsule: its two end spheres and the cylinder between. */
static float ray_capsule(br_vec3 from, br_vec3 d, const p3body *b, br_vec3 *n) {
    br_vec3 ax = br_v3_sub(b->seg_b, b->seg_a), m = br_v3_sub(from, b->seg_a);
    float hl = br_v3_len(ax), best = -1, t;
    int k;
    if (hl > 1e-6f) {                                          /* the side */
        br_vec3 u = br_v3_scale(ax, 1.0f / hl);
        br_vec3 dp = br_v3_sub(d, br_v3_scale(u, br_v3_dot(d, u))), mp = br_v3_sub(m, br_v3_scale(u, br_v3_dot(m, u)));
        float A = br_v3_dot(dp, dp), B = 2 * br_v3_dot(dp, mp), C = br_v3_dot(mp, mp) - b->radius * b->radius, disc = B * B - 4 * A * C;
        if (C <= 0) {                                          /* starting inside the side */
            float h = br_v3_dot(m, u);
            if (h >= 0 && h <= hl) { *n = br_v3_neg(d); return 0; }
        }
        if (A > 1e-12f && disc >= 0) {
            t = (-B - sqrtf(disc)) / (2 * A);
            if (t >= 0) {
                float h = br_v3_dot(br_v3_add_scaled(m, d, t), u);
                if (h >= 0 && h <= hl) {
                    br_vec3 p = br_v3_add_scaled(from, d, t);
                    best = t;
                    *n = br_v3_norm(br_v3_sub(p, br_v3_add_scaled(b->seg_a, u, h)));
                }
            }
        }
    }
    for (k = 0; k < 2; ++k) {                                  /* the ends */
        br_vec3 cen = k ? b->seg_b : b->seg_a;
        t = ray_sphere(from, d, cen, b->radius);
        if (t >= 0 && (best < 0 || t < best)) { best = t; *n = br_v3_norm(br_v3_sub(br_v3_add_scaled(from, d, t), cen)); }
    }
    return best;
}

bool br_physics3d_raycast(const br_world *w, br_vec3 from, br_vec3 to, int mask, br_entity ignore, br_raycast3d_hit *hit) {
    p3_ids id;
    const br_component_pool *pool;
    br_vec3 d = br_v3_sub(to, from);
    float len = br_v3_len(d), best = 1e30f;
    uint32_t i;
    bool found = false;
    if (!p3_ids_of(w, &id) || len < 1e-9f) return false;
    d = br_v3_scale(d, 1.0f / len);
    pool = &w->pools[id.col];
    for (i = 0; i < pool->count; ++i) {
        p3body b;
        float t = -1;
        br_vec3 n = br_v3(0, 1, 0);
        br_entity e = pool->entities[i];
        if (e == ignore || !p3_one(w, &id, e, &b)) continue;
        if (b.trigger || !(mask & (1 << (b.layer & 31)))) { p3_done(&b); continue; }
        if (b.shape == BR_COLLIDER3D_SPHERE) {
            t = ray_sphere(from, d, b.x, b.radius);
            if (t < 0) continue;
            n = br_v3_norm(br_v3_sub(br_v3_add(from, br_v3_scale(d, t)), b.x));
        } else if (b.shape == BR_COLLIDER3D_CAPSULE) {
            t = ray_capsule(from, d, &b, &n);
            if (t < 0) continue;
        } else if (b.shape == BR_COLLIDER3D_MESH) {            /* the Mesh's own triangles, in its space */
            br_mat4 m = br_world_matrix3d(w, e), inv = br_m4_inverse(m);
            br_vec3 lf = br_m4_mul_point(inv, from), lt = br_m4_mul_point(inv, to), ln;
            float u;
            bool hit_it = b.mesh && br_mesh_raycast(b.mesh, lf, br_v3_sub(lt, lf), &u, &ln) && u <= 1;
            p3_done(&b);
            if (!hit_it) continue;
            t = u * len;
            n = br_v3_norm(br_m4_mul_dir(br_m4_transpose(inv), ln));
            if (br_v3_dot(n, d) > 0) n = br_v3_neg(n);
        } else {                                               /* slabs, in the box's own axes */
            br_vec3 o = br_v3_sub(from, b.x);
            float tmin = 0, tmax = len;
            int k, axis = -1;
            float sgn = 1;
            bool miss = false;
            for (k = 0; k < 3 && !miss; ++k) {
                float oc = br_v3_dot(o, b.ax[k]), dc = br_v3_dot(d, b.ax[k]), h = v3c(b.half, k);
                if (fabsf(dc) < 1e-9f) { if (oc < -h || oc > h) miss = true; continue; }
                {
                    float t1 = (-h - oc) / dc, t2 = (h - oc) / dc, s1 = -1;
                    if (t1 > t2) { float tt = t1; t1 = t2; t2 = tt; s1 = 1; }
                    if (t1 > tmin) { tmin = t1; axis = k; sgn = s1; }
                    if (t2 < tmax) tmax = t2;
                    if (tmin > tmax) miss = true;
                }
            }
            if (miss) continue;
            t = tmin;
            n = axis >= 0 ? br_v3_scale(b.ax[axis], sgn) : br_v3_neg(d);
        }
        if (t >= 0 && t <= len && t < best) {
            best = t;
            found = true;
            if (hit) { hit->entity = e; hit->distance = t; hit->point = br_v3_add(from, br_v3_scale(d, t)); hit->normal = n; }
        }
    }
    return found;
}

uint32_t br_physics3d_overlap_sphere(const br_world *w, br_vec3 centre, float radius, int mask, br_entity *out, uint32_t max) {
    p3_ids id;
    const br_component_pool *pool;
    uint32_t i, n = 0;
    p3body probe;
    if (!p3_ids_of(w, &id)) return 0;
    memset(&probe, 0, sizeof(probe));
    probe.shape = BR_COLLIDER3D_SPHERE;
    probe.x = centre;
    probe.radius = radius;
    probe.q = br_quat_identity();
    p3_shape(&probe);
    pool = &w->pools[id.col];
    for (i = 0; i < pool->count; ++i) {
        p3body b;
        br_vec3 nn;
        p3contact c[P3_MAX_CONTACTS];
        br_entity e = pool->entities[i];
        int k;
        if (!p3_one(w, &id, e, &b)) continue;
        if (!(mask & (1 << (b.layer & 31)))) { p3_done(&b); continue; }
        k = p3_collide(&probe, &b, &nn, c);
        p3_done(&b);
        if (k <= 0) continue;
        if (out && n < max) out[n] = e;
        ++n;
    }
    return n;
}

void br_physics3d_debug_draw(const br_world *w, br_r3d *r3, br_color solid, br_color trigger) {
    p3_ids id;
    const br_component_pool *pool;
    uint32_t i;
    if (!p3_ids_of(w, &id)) return;
    pool = &w->pools[id.col];
    for (i = 0; i < pool->count; ++i) {
        p3body b;
        br_color c;
        int k;
        if (!p3_one(w, &id, pool->entities[i], &b)) continue;
        c = b.trigger ? trigger : solid;
        if (b.shape == BR_COLLIDER3D_MESH) {                   /* its triangles' edges */
            uint32_t t;
            if (b.mesh) for (t = 0; t + 2 < b.mesh->index_count && t < 3 * 4000; t += 3) {
                br_vec3 p0 = b.mv[b.mesh->indices[t]], p1 = b.mv[b.mesh->indices[t + 1]], p2 = b.mv[b.mesh->indices[t + 2]];
                br_r3d_line(r3, p0, p1, c, true); br_r3d_line(r3, p1, p2, c, true); br_r3d_line(r3, p2, p0, c, true);
            }
            p3_done(&b);
        } else if (b.shape == BR_COLLIDER3D_CAPSULE) {         /* the ends' rings, and four lines between */
            br_vec3 u = b.ax[0], v = b.ax[2];
            for (k = 0; k < 24; ++k) {
                float a0 = BR_TAU * (float)k / 24, a1 = BR_TAU * (float)(k + 1) / 24;
                br_vec3 o0 = br_v3_add(br_v3_scale(u, cosf(a0) * b.radius), br_v3_scale(v, sinf(a0) * b.radius));
                br_vec3 o1 = br_v3_add(br_v3_scale(u, cosf(a1) * b.radius), br_v3_scale(v, sinf(a1) * b.radius));
                br_r3d_line(r3, br_v3_add(b.seg_a, o0), br_v3_add(b.seg_a, o1), c, false);
                br_r3d_line(r3, br_v3_add(b.seg_b, o0), br_v3_add(b.seg_b, o1), c, false);
                if (k % 6 == 0) br_r3d_line(r3, br_v3_add(b.seg_a, o0), br_v3_add(b.seg_b, o0), c, false);
                if (k < 12) {                                  /* the caps' arcs */
                    float c0 = BR_PI * (float)k / 12, c1 = BR_PI * (float)(k + 1) / 12;
                    br_vec3 up = b.ax[1];
                    br_r3d_line(r3, br_v3_add(b.seg_b, br_v3_add(br_v3_scale(u, cosf(c0) * b.radius), br_v3_scale(up, sinf(c0) * b.radius))),
                                br_v3_add(b.seg_b, br_v3_add(br_v3_scale(u, cosf(c1) * b.radius), br_v3_scale(up, sinf(c1) * b.radius))), c, false);
                    br_r3d_line(r3, br_v3_add(b.seg_a, br_v3_add(br_v3_scale(u, cosf(c0) * b.radius), br_v3_scale(up, -sinf(c0) * b.radius))),
                                br_v3_add(b.seg_a, br_v3_add(br_v3_scale(u, cosf(c1) * b.radius), br_v3_scale(up, -sinf(c1) * b.radius))), c, false);
                }
            }
        } else if (b.shape == BR_COLLIDER3D_SPHERE) {          /* three rings */
            int ax;
            for (ax = 0; ax < 3; ++ax)
                for (k = 0; k < 24; ++k) {
                    float a0 = BR_TAU * (float)k / 24, a1 = BR_TAU * (float)(k + 1) / 24;
                    br_vec3 u = b.ax[(ax + 1) % 3], v = b.ax[(ax + 2) % 3];
                    br_vec3 p0 = br_v3_add(b.x, br_v3_add(br_v3_scale(u, cosf(a0) * b.radius), br_v3_scale(v, sinf(a0) * b.radius)));
                    br_vec3 p1 = br_v3_add(b.x, br_v3_add(br_v3_scale(u, cosf(a1) * b.radius), br_v3_scale(v, sinf(a1) * b.radius)));
                    br_r3d_line(r3, p0, p1, c, false);
                }
        } else {                                               /* twelve edges */
            br_vec3 corner[8];
            static const int edges[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            for (k = 0; k < 8; ++k)
                corner[k] = br_v3_add(b.x, br_v3_add(br_v3_add(br_v3_scale(b.ax[0], (k & 1 ? 1.0f : -1.0f) * b.half.x),
                                                               br_v3_scale(b.ax[1], (k & 2 ? 1.0f : -1.0f) * b.half.y)),
                                                     br_v3_scale(b.ax[2], (k & 4 ? 1.0f : -1.0f) * b.half.z)));
            for (k = 0; k < 12; ++k) br_r3d_line(r3, corner[edges[k][0]], corner[edges[k][1]], c, false);
        }
    }
}

/* ---- moving characters ------------------------------------------------------------------------ */

static void p3_remember_move(br_world *w, const br_physics3d_move_info *info) {
    p3state *s = p3_state(w);
    uint32_t i;
    if (!s) return;
    for (i = 0; i < s->moved_count; ++i)
        if (s->moved[i].entity == info->entity) { s->moved[i] = *info; return; }
    for (i = 0; i < s->moved_count; ++i)                       /* (room taken by entities that are gone) */
        if (!br_entity_alive(w, s->moved[i].entity)) { s->moved[i] = *info; return; }
    if (grow((void **)&s->moved, &s->moved_cap, s->moved_count + 1, sizeof(br_physics3d_move_info))) s->moved[s->moved_count++] = *info;
}

bool br_physics3d_last_move(const br_world *w, br_entity e, br_physics3d_move_info *info) {
    const p3state *s = (const p3state *)br_world_attached(w, &p3_state_key);
    uint32_t i;
    if (s) for (i = 0; i < s->moved_count; ++i)
        if (s->moved[i].entity == e) { if (info) *info = s->moved[i]; return true; }
    if (info) memset(info, 0, sizeof(*info));
    return false;
}

#define P3_FLOOR 0.7f                  /* a surface this flat (normal.y) is a floor */
#define P3_SKIN 0.001f

bool br_physics3d_move(br_world *w, br_entity e, br_vec3 motion, br_vec3 *velocity, br_physics3d_move_info *info) {
    p3_ids id;
    p3body me, *others = NULL;
    const br_component_pool *pool;
    br_physics3d_move_info got;
    br_vec3 pos, centre_offset, step;
    uint32_t i, n = 0, cap = 0;
    float size, len = br_v3_len(motion);
    int steps, k, it;
    memset(&got, 0, sizeof(got));
    got.entity = e;
    if (!p3_ids_of(w, &id) || !br_has(w, e, id.t3)) return false;
    br_world_transform3d(w, e, &pos, NULL, NULL);
    if (!p3_one(w, &id, e, &me) || me.shape == BR_COLLIDER3D_MESH) {   /* nothing to collide with: just move */
        p3_done(&me);
        br_set_world_position3d(w, e, br_v3_add(pos, motion));
        p3_remember_move(w, &got);
        if (info) *info = got;
        return false;
    }
    centre_offset = br_v3_sub(me.x, pos);
    size = me.shape == BR_COLLIDER3D_BOX ? BR_MIN(me.half.x, BR_MIN(me.half.y, me.half.z)) : me.radius;
    if (size < 0.01f) size = 0.01f;
    steps = (int)ceilf(len / (size * 0.5f));
    steps = steps < 1 ? 1 : steps > 32 ? 32 : steps;
    {   /* everything solid it could meet on the way */
        br_vec3 lo = br_v3_sub(br_v3_min(me.lo, br_v3_add(me.lo, motion)), br_v3(0.1f, 0.1f, 0.1f));
        br_vec3 hi = br_v3_add(br_v3_max(me.hi, br_v3_add(me.hi, motion)), br_v3(0.1f, 0.1f, 0.1f));
        pool = &w->pools[id.col];
        for (i = 0; i < pool->count; ++i) {
            br_entity o = pool->entities[i];
            p3body b;
            if (o == e || br_is_ancestor(w, e, o) || !p3_one(w, &id, o, &b)) continue;
            if (b.trigger || !p3_layers(&me, &b) || b.lo.x > hi.x || b.hi.x < lo.x || b.lo.y > hi.y || b.hi.y < lo.y || b.lo.z > hi.z || b.hi.z < lo.z ||
                !grow((void **)&others, &cap, n + 1, sizeof(p3body))) { p3_done(&b); continue; }
            others[n++] = b;
        }
    }
    step = br_v3_scale(motion, 1.0f / (float)steps);
    for (k = 0; k < steps; ++k) {
        me.x = br_v3_add(me.x, step);
        p3_shape(&me);
        for (it = 0; it < 4; ++it) {                          /* push out of what it went into */
            bool pushed = false;
            for (i = 0; i < n; ++i) {
                br_vec3 nn, out;
                p3contact c[P3_MAX_CONTACTS];
                int cnt, j;
                float depth = 0;
                if (others[i].lo.x > me.hi.x || others[i].hi.x < me.lo.x || others[i].lo.y > me.hi.y || others[i].hi.y < me.lo.y ||
                    others[i].lo.z > me.hi.z || others[i].hi.z < me.lo.z) continue;
                memset(c, 0, sizeof(c));
                cnt = p3_collide(&me, &others[i], &nn, c);
                for (j = 0; j < cnt; ++j) depth = br_maxf(depth, c[j].depth);
                if (cnt <= 0 || depth <= 1e-6f) continue;
                out = br_v3_neg(nn);                           /* the surface's normal, towards the mover */
                if (out.y >= P3_FLOOR) {                       /* a floor: straight up (no sliding down slopes) */
                    me.x.y += (depth + P3_SKIN) / out.y;
                    if (!got.on_floor || out.y > got.floor_normal.y) got.floor_normal = out;
                    got.on_floor = true;
                } else {
                    if (out.y > 0 && out.y < P3_FLOOR) {             /* too steep to stand on: a wall, so sliding */
                        br_vec3 flat = br_v3(out.x, 0, out.z);        /* along it never carries the mover up it */
                        float fl = br_v3_len(flat);
                        if (fl > 1e-4f) {
                            depth /= fl;
                            out = br_v3_scale(flat, 1.0f / fl);
                        }
                    }
                    me.x = br_v3_add_scaled(me.x, out, depth + P3_SKIN);
                    if (out.y <= -P3_FLOOR) got.on_ceiling = true;
                    else got.on_wall = true;
                }
                {   /* what's left of the move, and the velocity, no longer go into it */
                    float into = br_v3_dot(step, out);
                    if (into < 0) step = br_v3_add_scaled(step, out, -into);
                    if (velocity && (into = br_v3_dot(*velocity, out)) < 0) *velocity = br_v3_add_scaled(*velocity, out, -into);
                }
                p3_shape(&me);
                pushed = true;
            }
            if (!pushed) break;
        }
    }
    if (!got.on_floor && n) {                                  /* standing still on something: still on the floor */
        p3body probe = me;
        probe.x.y -= 0.02f;
        p3_shape(&probe);
        for (i = 0; i < n && !got.on_floor; ++i) {
            br_vec3 nn;
            p3contact c[P3_MAX_CONTACTS];
            memset(c, 0, sizeof(c));
            if (p3_collide(&probe, &others[i], &nn, c) > 0 && -nn.y >= P3_FLOOR) {
                got.on_floor = true;
                got.floor_normal = br_v3_neg(nn);
                if (velocity && velocity->y < 0) velocity->y = 0;
            }
        }
    }
    for (i = 0; i < n; ++i) p3_done(&others[i]);
    br_free(others);
    p3_done(&me);
    br_set_world_position3d(w, e, br_v3_sub(me.x, centre_offset));
    p3_remember_move(w, &got);
    if (info) *info = got;
    return got.on_floor || got.on_wall || got.on_ceiling;
}

/* ---- registration ------------------------------------------------------------------------------ */

static void p3_body_added(br_world *w, br_entity e, void *data, void *user) {
    br_rigidbody3d *b = (br_rigidbody3d *)data;
    BR_UNUSED(w); BR_UNUSED(e); BR_UNUSED(user);
    b->force = br_v3(0, 0, 0);
    b->torque = br_v3(0, 0, 0);
}

static void p3_sys(br_world *w, float dt, void *user) { BR_UNUSED(user); br_physics3d_step(w, dt); }

void br_physics3d_register(br_world *w) {
    int i;
    br_component body;
    for (i = 0; i < (int)BR_ARRAY_COUNT(k_p3_descs); ++i) br_component_register_desc(w, &k_p3_descs[i]);
    body = br_component_find(w, "RigidBody3D");
    br_component_set_hooks(w, body, p3_body_added, NULL, NULL);
    br_system_add(w, "builtin_physics3d", BR_PHASE_FIXED_UPDATE, p3_sys, NULL, 21);
}
