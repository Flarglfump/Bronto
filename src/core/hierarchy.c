/*
 * Bronto Engine - the scene tree (see hierarchy.h).
 *
 * The tree is stored only as Parent components, so it saves, loads,
 * clones and undoes like any other data. Children are found by scanning
 * the Parent pool, which is small next to everything else a frame does.
 */
#include "bronto/hierarchy.h"
#include "bronto/memory.h"

#include <math.h>
#include <string.h>

#define MAX_DEPTH 64                 /* deeper chains are treated as broken */

static const br_field k_parent_fields[] = { BR_FIELD(br_parent, entity, BR_FIELD_ENTITY, 0, 0) };
static const br_parent k_parent_default = { BR_ENTITY_NULL };
static const br_component_desc k_parent_desc = {
    "Parent", sizeof(br_parent), k_parent_fields, (int)BR_ARRAY_COUNT(k_parent_fields), &k_parent_default
};
const br_component_desc *br_hierarchy_desc(void) { return &k_parent_desc; }

static br_component hy_parent_id(const br_world *w) { return br_component_find(w, "Parent"); }
static br_component hy_transform_id(const br_world *w) { return br_component_find(w, "Transform"); }

br_entity br_parent_of(const br_world *w, br_entity e) {
    br_component c = hy_parent_id(w);
    const br_parent *p = c == BR_COMPONENT_INVALID ? NULL : (const br_parent *)br_get(w, e, c);
    return p && br_entity_alive(w, p->entity) ? p->entity : BR_ENTITY_NULL;
}

bool br_is_ancestor(const br_world *w, br_entity ancestor, br_entity e) {
    int guard = 0;
    br_entity p = br_parent_of(w, e);
    while (p != BR_ENTITY_NULL && guard++ < MAX_DEPTH) {
        if (p == ancestor) return true;
        p = br_parent_of(w, p);
    }
    return false;
}

uint32_t br_depth(const br_world *w, br_entity e) {
    uint32_t d = 0;
    br_entity p = br_parent_of(w, e);
    while (p != BR_ENTITY_NULL && d < MAX_DEPTH) { ++d; p = br_parent_of(w, p); }
    return d;
}

uint32_t br_children(const br_world *w, br_entity e, br_entity *out, uint32_t cap) {
    br_component c = hy_parent_id(w);
    const br_component_pool *p;
    uint32_t i, n = 0;
    if (c == BR_COMPONENT_INVALID || e == BR_ENTITY_NULL) return 0;
    p = &w->pools[c];
    for (i = 0; i < p->count; ++i)
        if (((const br_parent *)(p->data + (size_t)i * p->stride))->entity == e) {
            if (out && n < cap) out[n] = p->entities[i];
            ++n;
        }
    return n;
}

br_entity br_child_named(const br_world *w, br_entity e, const char *name) {
    br_component c = hy_parent_id(w), nm = br_component_find(w, "Name");
    const br_component_pool *p;
    uint32_t i;
    if (c == BR_COMPONENT_INVALID || nm == BR_COMPONENT_INVALID) return BR_ENTITY_NULL;
    p = &w->pools[c];
    for (i = 0; i < p->count; ++i) {
        const br_name *n;
        if (((const br_parent *)(p->data + (size_t)i * p->stride))->entity != e) continue;
        n = (const br_name *)br_get(w, p->entities[i], nm);
        if (n && strcmp(n->text, name) == 0) return p->entities[i];
    }
    return BR_ENTITY_NULL;
}

uint32_t br_subtree(const br_world *w, br_entity e, br_entity *out, uint32_t cap) {
    br_entity kids[256];
    uint32_t n = 0, k, count;
    if (!br_entity_alive(w, e)) return 0;
    if (out && n < cap) out[n] = e;
    ++n;
    count = br_children(w, e, kids, 256);
    for (k = 0; k < count && k < 256; ++k)
        n += br_subtree(w, kids[k], out && n < cap ? out + n : NULL, n < cap ? cap - n : 0);
    return n;
}

/* ---- transforms ---------------------------------------------------------------- */

static br_transform identity(void) {
    br_transform t;
    t.position = br_v2(0, 0);
    t.rotation = 0;
    t.scale = br_v2(1, 1);
    return t;
}

br_transform br_compose(const br_transform *parent, const br_transform *local) {
    br_transform t;
    float a = parent->rotation * (BR_PI / 180.0f), c = cosf(a), s = sinf(a);
    br_vec2 q = br_v2(local->position.x * parent->scale.x, local->position.y * parent->scale.y);
    t.position = br_v2(parent->position.x + q.x * c - q.y * s, parent->position.y + q.x * s + q.y * c);
    t.rotation = parent->rotation + local->rotation;
    t.scale = br_v2(parent->scale.x * local->scale.x, parent->scale.y * local->scale.y);
    return t;
}

static br_transform world_of(const br_world *w, br_entity e, br_component tc, int depth) {
    const br_transform *local = (const br_transform *)br_get(w, e, tc);
    br_entity p = br_parent_of(w, e);
    br_transform base = p != BR_ENTITY_NULL && depth < MAX_DEPTH ? world_of(w, p, tc, depth + 1) : identity();
    if (!local) return base;                              /* a grouping node */
    if (p == BR_ENTITY_NULL) return *local;
    return br_compose(&base, local);
}

br_transform br_world_transform(const br_world *w, br_entity e) {
    br_component tc = hy_transform_id(w);
    if (tc == BR_COMPONENT_INVALID) return identity();
    return world_of(w, e, tc, 0);
}

static br_vec2 inverse_apply(const br_transform *p, br_vec2 v) {   /* undo rotation and scale */
    float a = -p->rotation * (BR_PI / 180.0f), c = cosf(a), s = sinf(a);
    br_vec2 r = br_v2(v.x * c - v.y * s, v.x * s + v.y * c);
    r.x = br_absf(p->scale.x) > 1e-6f ? r.x / p->scale.x : 0;
    r.y = br_absf(p->scale.y) > 1e-6f ? r.y / p->scale.y : 0;
    return r;
}

br_transform br_local_from_world(const br_world *w, br_entity e, const br_transform *world) {
    br_entity p = br_parent_of(w, e);
    br_transform pw, t;
    if (p == BR_ENTITY_NULL) return *world;
    pw = br_world_transform(w, p);
    t.position = inverse_apply(&pw, br_v2_sub(world->position, pw.position));
    t.rotation = world->rotation - pw.rotation;
    t.scale = br_v2(br_absf(pw.scale.x) > 1e-6f ? world->scale.x / pw.scale.x : world->scale.x,
                    br_absf(pw.scale.y) > 1e-6f ? world->scale.y / pw.scale.y : world->scale.y);
    return t;
}

br_vec2 br_world_to_parent_vector(const br_world *w, br_entity e, br_vec2 v) {
    br_entity p = br_parent_of(w, e);
    br_transform pw;
    if (p == BR_ENTITY_NULL) return v;
    pw = br_world_transform(w, p);
    return inverse_apply(&pw, v);
}

void br_set_world_position(br_world *w, br_entity e, br_vec2 position) {
    br_transform *t = (br_transform *)br_get(w, e, hy_transform_id(w));
    br_transform world, local;
    if (!t) return;
    world = br_world_transform(w, e);
    world.position = position;
    local = br_local_from_world(w, e, &world);
    t->position = local.position;
}

void br_set_world_rotation(br_world *w, br_entity e, float degrees) {
    br_transform *t = (br_transform *)br_get(w, e, hy_transform_id(w));
    br_entity p = br_parent_of(w, e);
    if (!t) return;
    t->rotation = p == BR_ENTITY_NULL ? degrees : degrees - br_world_transform(w, p).rotation;
}

bool br_set_parent(br_world *w, br_entity child, br_entity parent, bool keep_world) {
    br_component pc = hy_parent_id(w), tc = hy_transform_id(w);
    br_transform world;
    br_transform *t;
    if (pc == BR_COMPONENT_INVALID || !br_entity_alive(w, child)) return false;
    if (parent != BR_ENTITY_NULL && (!br_entity_alive(w, parent) || parent == child || br_is_ancestor(w, child, parent))) return false;
    if (br_parent_of(w, child) == parent) return true;
    world = br_world_transform(w, child);
    if (parent == BR_ENTITY_NULL) br_remove(w, child, pc);
    else {
        br_parent *p = (br_parent *)br_get(w, child, pc);
        if (p) br_remove(w, child, pc);            /* re-added: children keep the order they were parented in */
        p = (br_parent *)br_add(w, child, pc, NULL);
        if (!p) return false;
        p->entity = parent;
    }
    if (keep_world && tc != BR_COMPONENT_INVALID && (t = (br_transform *)br_get(w, child, tc)) != NULL)
        *t = br_local_from_world(w, child, &world);
    return true;
}

/* ---- destruction --------------------------------------------------------------------- */

void br_destroy_tree(br_world *w, br_entity e) { br_entity_destroy(w, e); }   /* the hook does the rest */

/* After an entity is destroyed: its children go too (and theirs, by recursion). */
static void on_destroyed(br_world *w, br_entity e, void *user) {
    br_component c = hy_parent_id(w);
    br_entity kids[128];
    uint32_t n, i;
    BR_UNUSED(user);
    if (c == BR_COMPONENT_INVALID || w->pools[c].count == 0) return;
    do {
        n = br_children(w, e, kids, 128);
        for (i = 0; i < n && i < 128; ++i) br_entity_destroy(w, kids[i]);
    } while (n > 128);
}

void br_hierarchy_register(br_world *w) {
    br_component_register_desc(w, &k_parent_desc);
    br_world_on_destroy(w, on_destroyed, NULL);
}
