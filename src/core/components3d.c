/*
 * Bronto Engine - 3D components: Transform3D, Mesh, Camera, Light (components3d.h).
 */
#include "bronto/components3d.h"
#include "bronto/hierarchy.h"
#include "bronto/assets.h"
#include "bronto/engine.h"
#include "bronto/memory.h"
#include "bronto/particles.h"
#include "bronto/model.h"

#include <math.h>
#include <string.h>

/* ---- descriptions --------------------------------------------------------------- */

static const br_field k_t3_fields[] = {
    BR_FIELD(br_transform3d, position, BR_FIELD_VEC3, 0, 0),
    BR_FIELD(br_transform3d, rotation, BR_FIELD_VEC3, 0, 0),
    BR_FIELD(br_transform3d, scale, BR_FIELD_VEC3, 0, 0),
};
static const br_transform3d k_t3_default = { { 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1 } };

static const char *const k_mesh_shapes[] = { "cube", "sphere", "plane", "cylinder", "cone", "capsule", "model" };
static const br_field k_mesh_fields[] = {
    BR_FIELD_ENUM_OF(br_mesh3d, shape, k_mesh_shapes),
    BR_FIELD(br_mesh3d, file, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_mesh3d, texture, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_mesh3d, color, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(br_mesh3d, lit, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_mesh3d, cull, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_mesh3d, visible, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_mesh3d, uv_scale, BR_FIELD_VEC2, 0, 0),
    BR_FIELD(br_mesh3d, shadow, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_mesh3d, cast_shadow, BR_FIELD_BOOL, 0, 0),
};
static const br_mesh3d k_mesh_default = { BR_MESH_CUBE, "", "", { 200, 200, 210, 255 }, true, true, true, { 1, 1 }, false, true };

static const char *const k_projections3d[] = { "perspective", "orthographic" };
static const br_field k_camera3d_fields[] = {
    BR_FIELD_ENUM_OF(br_camera3d_comp, projection, k_projections3d),
    BR_FIELD(br_camera3d_comp, fov, BR_FIELD_FLOAT, 1, 179),
    BR_FIELD(br_camera3d_comp, size, BR_FIELD_FLOAT, 0, 0),
    BR_FIELD(br_camera3d_comp, near_clip, BR_FIELD_FLOAT, 0, 0),
    BR_FIELD(br_camera3d_comp, far_clip, BR_FIELD_FLOAT, 0, 0),
    BR_FIELD(br_camera3d_comp, active, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_camera3d_comp, priority, BR_FIELD_INT, -100, 100),
    BR_FIELD(br_camera3d_comp, clear, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_camera3d_comp, clear_color, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(br_camera3d_comp, fog, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_camera3d_comp, fog_color, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(br_camera3d_comp, fog_start, BR_FIELD_FLOAT, 0, 0),
    BR_FIELD(br_camera3d_comp, fog_end, BR_FIELD_FLOAT, 0, 0),
};
static const br_camera3d_comp k_camera3d_default = { BR_PROJECTION_PERSPECTIVE, 60, 5, 0.1f, 500, true, 0, true, { 120, 170, 230, 255 },
                                                     false, { 170, 190, 215, 255 }, 20, 80 };

static const char *const k_light_kinds[] = { "directional", "point", "ambient" };
static const br_field k_light_fields[] = {
    BR_FIELD_ENUM_OF(br_light, kind, k_light_kinds),
    BR_FIELD(br_light, color, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(br_light, intensity, BR_FIELD_FLOAT, 0, 0),
    BR_FIELD(br_light, range, BR_FIELD_FLOAT, 0, 0),
    BR_FIELD(br_light, shadows, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_light, shadow_range, BR_FIELD_FLOAT, 1, 500),
};
static const br_light k_light_default = { BR_LIGHT_DIRECTIONAL, { 255, 248, 236, 255 }, 1, 10, false, 20 };

static const br_field k_animator_fields[] = {
    BR_FIELD(br_animator, clip, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_animator, speed, BR_FIELD_FLOAT, -10, 10),
    BR_FIELD(br_animator, loop, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_animator, playing, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_animator, blend, BR_FIELD_FLOAT, 0, 5),
};
static const br_animator k_animator_default = { "", 1, true, true, 0.2f, 0, "", "", 0, 0, 0 };

#define DESC3(name, T, fields, def) { name, sizeof(T), fields, (int)BR_ARRAY_COUNT(fields), def }
static const br_component_desc k_descs3[] = {
    DESC3("Transform3D", br_transform3d, k_t3_fields, &k_t3_default),
    DESC3("Mesh", br_mesh3d, k_mesh_fields, &k_mesh_default),
    DESC3("Camera3D", br_camera3d_comp, k_camera3d_fields, &k_camera3d_default),
    DESC3("Light", br_light, k_light_fields, &k_light_default),
    DESC3("Animator", br_animator, k_animator_fields, &k_animator_default),
};
static const br_component_desc *const k_desc3_list[] = { &k_descs3[0], &k_descs3[1], &k_descs3[2], &k_descs3[3], &k_descs3[4] };

const br_component_desc *const *br_components3d_descs(int *count) {
    *count = (int)BR_ARRAY_COUNT(k_desc3_list);
    return k_desc3_list;
}

bool br_ids3d_of(const br_world *w, br_ids3d *ids) {
    ids->transform3d = br_component_find(w, "Transform3D");
    ids->mesh = br_component_find(w, "Mesh");
    ids->camera = br_component_find(w, "Camera3D");
    ids->light = br_component_find(w, "Light");
    return ids->transform3d != BR_COMPONENT_INVALID && ids->mesh != BR_COMPONENT_INVALID &&
           ids->camera != BR_COMPONENT_INVALID && ids->light != BR_COMPONENT_INVALID;
}

/* ---- transforms ------------------------------------------------------------------- */

br_mat4 br_transform3d_matrix(const br_transform3d *t) {
    return br_m4_trs(t->position, br_quat_from_euler(t->rotation), t->scale);
}

static br_component t3_id(const br_world *w) { return br_component_find(w, "Transform3D"); }

br_mat4 br_world_matrix3d(const br_world *w, br_entity e) {
    br_component tc = t3_id(w);
    br_mat4 m = br_m4_identity();
    int guard = 0;
    if (tc == BR_COMPONENT_INVALID) return m;
    while (e != BR_ENTITY_NULL && guard++ < 256) {               /* outward: parent * ... * own */
        const br_transform3d *t = (const br_transform3d *)br_get(w, e, tc);
        if (t) m = br_m4_mul(br_transform3d_matrix(t), m);
        e = br_parent_of(w, e);
    }
    return m;
}

void br_world_transform3d(const br_world *w, br_entity e, br_vec3 *position, br_quat *rotation, br_vec3 *scale) {
    br_m4_decompose(br_world_matrix3d(w, e), position, rotation, scale);
}

void br_set_world_matrix3d(br_world *w, br_entity e, br_mat4 world) {
    br_component tc = t3_id(w);
    br_transform3d *t = tc == BR_COMPONENT_INVALID ? NULL : (br_transform3d *)br_get(w, e, tc);
    br_entity p = br_parent_of(w, e);
    br_mat4 local = p != BR_ENTITY_NULL ? br_m4_mul(br_m4_inverse(br_world_matrix3d(w, p)), world) : world;
    br_quat q;
    if (!t) return;
    br_m4_decompose(local, &t->position, &q, &t->scale);
    t->rotation = br_quat_to_euler(q);
}

void br_set_world_position3d(br_world *w, br_entity e, br_vec3 position) {
    br_mat4 m = br_world_matrix3d(w, e);
    m.m[12] = position.x; m.m[13] = position.y; m.m[14] = position.z;
    br_set_world_matrix3d(w, e, m);
}

void br_set_world_rotation3d(br_world *w, br_entity e, br_quat rotation) {
    br_vec3 p, s;
    br_world_transform3d(w, e, &p, NULL, &s);
    br_set_world_matrix3d(w, e, br_m4_trs(p, rotation, s));
}

br_vec3 br_forward3d(const br_world *w, br_entity e) { return br_v3_norm(br_m4_mul_dir(br_world_matrix3d(w, e), br_v3(0, 0, -1))); }
br_vec3 br_right3d(const br_world *w, br_entity e) { return br_v3_norm(br_m4_mul_dir(br_world_matrix3d(w, e), br_v3(1, 0, 0))); }
br_vec3 br_up3d(const br_world *w, br_entity e) { return br_v3_norm(br_m4_mul_dir(br_world_matrix3d(w, e), br_v3(0, 1, 0))); }

void br_look_at3d(br_world *w, br_entity e, br_vec3 target) {
    br_vec3 p;
    br_world_transform3d(w, e, &p, NULL, NULL);
    if (br_v3_dist(p, target) < 1e-6f) return;
    br_set_world_rotation3d(w, e, br_quat_look(br_v3_sub(target, p), br_v3(0, 1, 0)));
}

/* ---- meshes -------------------------------------------------------------------------- */

static struct { br_mesh prims[6]; bool made[6]; br_mesh disk; br_image soft; bool disk_made; } P3;
/* glTF models' scratch: the parts posed, the node matrices, two poses (for crossfades) */
static struct { br_mesh *posed; int posed_count; br_mat4 *nodes; int node_cap; br_model_pose a, b; const br_model *pose_of; } MD;

const br_mesh *br_mesh3d_mesh(const br_mesh3d *m) {
    int s = m->shape;
    if (s == BR_MESH_MODEL) return m->file[0] ? br_asset_mesh(m->file) : NULL;
    if (s < 0 || s > BR_MESH_CAPSULE) s = BR_MESH_CUBE;
    if (!P3.made[s]) {
        bool ok = false;
        switch (s) {
        case BR_MESH_CUBE: ok = br_mesh_cube(&P3.prims[s]); break;
        case BR_MESH_SPHERE: ok = br_mesh_sphere(&P3.prims[s], 24, 14); break;
        case BR_MESH_PLANE: ok = br_mesh_plane(&P3.prims[s]); break;
        case BR_MESH_CYLINDER: ok = br_mesh_cylinder(&P3.prims[s], 24); break;
        case BR_MESH_CAPSULE: ok = br_mesh_capsule(&P3.prims[s], 20, 12); break;
        default: ok = br_mesh_cone(&P3.prims[s], 24); break;
        }
        if (!ok) return NULL;
        P3.made[s] = true;
    }
    return &P3.prims[s];
}

void br_components3d_release(void) {
    int i;
    for (i = 0; i < MD.posed_count; ++i) br_mesh_destroy(&MD.posed[i]);
    br_free(MD.posed);
    br_free(MD.nodes);
    br_model_pose_destroy(&MD.a);
    br_model_pose_destroy(&MD.b);
    memset(&MD, 0, sizeof(MD));
    for (i = 0; i < 6; ++i) if (P3.made[i]) br_mesh_destroy(&P3.prims[i]);
    if (P3.disk_made) { br_mesh_destroy(&P3.disk); br_image_destroy(&P3.soft); }
    memset(&P3, 0, sizeof(P3));
}

/* ---- cameras and drawing ---------------------------------------------------------------- */

static br_camera3d camera_of(const br_world *w, br_entity e, const br_camera3d_comp *c) {
    br_camera3d k;
    br_vec3 s;
    memset(&k, 0, sizeof(k));
    br_world_transform3d(w, e, &k.position, &k.rotation, &s);
    k.orthographic = c->projection == BR_PROJECTION_ORTHOGRAPHIC;
    k.fov = c->fov;
    k.size = c->size;
    k.near_clip = c->near_clip;
    k.far_clip = c->far_clip;
    return k;
}

bool br_scene_camera3d(const br_world *w, br_camera3d *out, br_entity *which) {
    br_ids3d ids;
    const br_component_pool *pool;
    uint32_t i;
    int best = -1000000;
    br_entity found = BR_ENTITY_NULL;
    if (!br_ids3d_of(w, &ids)) return false;
    pool = &w->pools[ids.camera];
    for (i = 0; i < pool->count; ++i) {
        const br_camera3d_comp *c = (const br_camera3d_comp *)(pool->data + (size_t)i * pool->stride);
        br_entity e = pool->entities[i];
        if (!c->active || !br_has(w, e, ids.transform3d) || c->priority <= best) continue;
        best = c->priority;
        found = e;
    }
    if (found == BR_ENTITY_NULL) return false;
    if (out) *out = camera_of(w, found, (const br_camera3d_comp *)br_get(w, found, ids.camera));
    if (which) *which = found;
    return true;
}

bool br_world_has_3d(const br_world *w) {
    br_ids3d ids;
    if (!br_ids3d_of(w, &ids)) return false;
    return br_component_count(w, ids.transform3d) > 0;
}

typedef struct draw3_item { br_entity e; float dist; } draw3_item;

static void sort_far_first(draw3_item *it, uint32_t n) {   /* insertion sort: few see-through things */
    uint32_t i, j;
    for (i = 1; i < n; ++i) {
        draw3_item x = it[i];
        for (j = i; j > 0 && it[j - 1].dist < x.dist; --j) it[j] = it[j - 1];
        it[j] = x;
    }
}

/* ---- glTF models: posed by their Animator -------------------------------------------- */


static const br_model *model_of(const br_mesh3d *m) {
    return m && m->shape == BR_MESH_MODEL && br_model_is_file(m->file) ? br_asset_model(m->file) : NULL;
}

static const br_animator *animator_of(const br_world *w, br_entity e) {
    br_component c = br_component_find(w, "Animator");
    return c != BR_COMPONENT_INVALID ? (const br_animator *)br_get(w, e, c) : NULL;
}

/* The node matrices of a model as its entity's Animator has it now. */
static const br_mat4 *model_nodes(const br_world *w, br_entity e, const br_model *md) {
    const br_animator *an = animator_of(w, e);
    if (md->node_count > MD.node_cap) {
        br_mat4 *g = (br_mat4 *)br_realloc(MD.nodes, sizeof(br_mat4) * (size_t)md->node_count);
        if (!g) return NULL;
        MD.nodes = g;
        MD.node_cap = md->node_count;
    }
    if (MD.pose_of != md || MD.a.count != md->node_count) {      /* (pose buffers the size of this model) */
        br_model_pose_destroy(&MD.a);
        br_model_pose_destroy(&MD.b);
        if (!br_model_pose_init(&MD.a, md) || !br_model_pose_init(&MD.b, md)) { MD.pose_of = NULL; return NULL; }
        MD.pose_of = md;
    }
    if (an) {
        br_model_pose_sample(&MD.a, md, br_model_find_clip(md, an->started[0] ? an->started : an->clip), an->time, an->loop);
        if (an->from[0] && an->fade_len > 0 && an->fade < an->fade_len) {   /* crossfading from the last clip */
            br_model_pose_sample(&MD.b, md, br_model_find_clip(md, an->from), an->from_time, true);
            br_model_pose_blend(&MD.b, &MD.a, an->fade / an->fade_len);
            br_model_node_matrices(md, &MD.b, MD.nodes);
            return MD.nodes;
        }
    } else {
        br_model_pose_rest(&MD.a, md);
    }
    br_model_node_matrices(md, &MD.a, MD.nodes);
    return MD.nodes;
}

/* Each part of the model, posed: draws them (mat != NULL) or adds them to the shadow map. */
static void model_parts(const br_world *w, br_r3d *r3, br_entity e, const br_mesh3d *m, const br_model *md, const br_material *base) {
    const br_mat4 *nodes = model_nodes(w, e, md);
    br_mat4 world = br_world_matrix3d(w, e);
    int i;
    if (!nodes) return;
    if (md->part_count > MD.posed_count) {
        br_mesh *g = (br_mesh *)br_realloc(MD.posed, sizeof(br_mesh) * (size_t)md->part_count);
        if (!g) return;
        memset(g + MD.posed_count, 0, sizeof(br_mesh) * (size_t)(md->part_count - MD.posed_count));
        MD.posed = g;
        MD.posed_count = md->part_count;
    }
    for (i = 0; i < md->part_count; ++i) {
        const br_model_part *part = &md->parts[i];
        if (!br_model_part_posed(md, i, nodes, &MD.posed[i])) continue;
        if (!base) { br_r3d_shadow_mesh(r3, &MD.posed[i], world); continue; }
        {
            br_material mat = *base;
            mat.color = br_rgba((uint8_t)(m->color.r * part->color.r / 255), (uint8_t)(m->color.g * part->color.g / 255),
                                (uint8_t)(m->color.b * part->color.b / 255), (uint8_t)(m->color.a * part->color.a / 255));
            if (!mat.texture) mat.texture = part->texture;
            br_r3d_draw_mesh(r3, &MD.posed[i], world, &mat);
        }
    }
}

static void draw_one(const br_world *w, br_r3d *r3, const br_ids3d *ids, br_entity e) {
    const br_mesh3d *m = (const br_mesh3d *)br_get(w, e, ids->mesh);
    const br_mesh *mesh;
    const br_model *md;
    br_material mat;
    if (m && m->visible && (md = model_of(m)) != NULL) {         /* a glTF model: its parts, posed */
        mat = br_material_make(m->color);
        mat.lit = m->lit;
        mat.cull = m->cull;
        mat.uv_scale = m->uv_scale;
        mat.texture = m->texture[0] ? br_asset_image(m->texture) : NULL;
        model_parts(w, r3, e, m, md, &mat);
        return;
    }
    if (!m || !m->visible || !(mesh = br_mesh3d_mesh(m))) return;
    mat = br_material_make(m->color);
    mat.lit = m->lit;
    mat.cull = m->cull;
    mat.uv_scale = m->uv_scale;
    mat.texture = m->texture[0] ? br_asset_image(m->texture) : NULL;
    br_r3d_draw_mesh(r3, mesh, br_world_matrix3d(w, e), &mat);
}

/* Round shadows: for each Mesh with `shadow`, a soft dark disc on the first
 * thing straight below it, as wide as it is, fading as it gets higher. */
static void draw_shadows(const br_world *w, br_r3d *r3, const br_ids3d *ids) {
    const br_component_pool *pool = &w->pools[ids->mesh];
    br_material mat;
    uint32_t i;
    if (!P3.disk_made) {                                          /* a flat disc, radius 1, facing up, with a soft edge */
        int k, seg = 20, x, y;
        br_mesh *m = &P3.disk;
        if (!br_image_init(&P3.soft, 32, 32)) return;
        for (y = 0; y < 32; ++y)
            for (x = 0; x < 32; ++x) {
                float dx = ((float)x + 0.5f) / 16.0f - 1, dy = ((float)y + 0.5f) / 16.0f - 1, d = sqrtf(dx * dx + dy * dy);
                float a = d >= 1 ? 0 : d < 0.4f ? 1 : 1 - (d - 0.4f) / 0.6f;
                br_image_set(&P3.soft, x, y, br_rgba(255, 255, 255, (uint8_t)(a * a * 255)));
            }
        if (!br_mesh_init(m, (uint32_t)(seg + 1) * 2, (uint32_t)seg * 3 * 3)) { br_image_destroy(&P3.soft); return; }
        memset(m->verts, 0, sizeof(br_vertex3d) * m->vert_count);
        m->verts[0].normal = br_v3(0, 1, 0);
        m->verts[0].uv = br_v2(0.5f, 0.5f);
        for (k = 0; k < seg; ++k) {
            float a = BR_TAU * (float)k / (float)seg;
            br_vertex3d *in = &m->verts[1 + k], *out = &m->verts[1 + seg + k];
            in->pos = br_v3(cosf(a) * 0.6f, 0, -sinf(a) * 0.6f);
            out->pos = br_v3(cosf(a), 0, -sinf(a));
            in->normal = out->normal = br_v3(0, 1, 0);
            in->uv = br_v2(0.5f + in->pos.x * 0.5f, 0.5f - in->pos.z * 0.5f);   /* (the soft image, laid flat) */
            out->uv = br_v2(0.5f + out->pos.x * 0.5f, 0.5f - out->pos.z * 0.5f);
        }
        for (k = 0; k < seg; ++k) {
            uint32_t a = 1 + (uint32_t)k, b = 1 + (uint32_t)((k + 1) % seg), c = a + (uint32_t)seg, d = b + (uint32_t)seg, *ix = m->indices + k * 9;
            ix[0] = 0; ix[1] = a; ix[2] = b;
            ix[3] = a; ix[4] = c; ix[5] = d;
            ix[6] = a; ix[7] = d; ix[8] = b;
        }
        br_mesh_bounds(m);
        P3.disk_made = true;
    }
    mat = br_material_make(br_rgba(0, 0, 0, 0));
    mat.texture = &P3.soft;
    mat.lit = false;
    mat.cull = false;
    for (i = 0; i < pool->count; ++i) {
        const br_mesh3d *m = (const br_mesh3d *)(pool->data + (size_t)i * pool->stride);
        br_entity e = pool->entities[i], hit;
        const br_mesh *mesh;
        br_mat4 world;
        br_vec3 lo, hi, centre, n;
        float t, radius, alpha;
        int k;
        if (!m->shadow || !m->visible || !br_has(w, e, ids->transform3d) || !(mesh = br_mesh3d_mesh(m))) continue;
        world = br_world_matrix3d(w, e);
        for (k = 0; k < 8; ++k) {                                 /* its box in the world */
            br_vec3 p = br_m4_mul_point(world, br_v3(k & 1 ? mesh->max.x : mesh->min.x, k & 2 ? mesh->max.y : mesh->min.y, k & 4 ? mesh->max.z : mesh->min.z));
            lo = k ? br_v3_min(lo, p) : p;
            hi = k ? br_v3_max(hi, p) : p;
        }
        centre = br_v3((lo.x + hi.x) * 0.5f, lo.y - 0.01f, (lo.z + hi.z) * 0.5f);
        radius = 0.5f * BR_MAX(hi.x - lo.x, hi.z - lo.z);
        hit = br_pick3d(w, centre, br_v3(0, -1, 0), &t, &n);
        if (hit == BR_ENTITY_NULL || hit == e || t > 8 || n.y < 0.3f) continue;
        alpha = 120.0f * (1.0f - t / 8.0f);
        radius *= 1.0f + t * 0.15f;                               /* higher: wider and fainter */
        {
            br_vec3 at = br_v3(centre.x, centre.y - t, centre.z);
            br_quat q = br_quat_look(br_v3_norm(br_v3_cross(br_v3(1, 0, 0), n)), n);   /* its up along the surface's normal */
            br_mat4 model = br_m4_trs(br_v3_add_scaled(at, n, 0.02f), q, br_v3(radius, 1, radius));
            mat.color = br_rgba(0, 0, 0, (uint8_t)alpha);
            br_r3d_draw_mesh(r3, &P3.disk, model, &mat);
        }
    }
}

bool br_draw3d(const br_world *w, br_r3d *r3, br_renderer *r, const br_camera3d *camera, br_recti viewport) {
    br_ids3d ids;
    br_camera3d cam;
    br_entity cam_e = BR_ENTITY_NULL;
    const br_component_pool *pool;
    draw3_item *see_through = NULL;
    uint32_t i, ns = 0;
    int lights = 0, shadow_light = -1;
    float shadow_range = 20;
    if (!br_ids3d_of(w, &ids)) return false;
    if (camera) cam = *camera;
    else if (!br_scene_camera3d(w, &cam, &cam_e)) return false;
    if (!camera && cam_e != BR_ENTITY_NULL) {                   /* the scene camera's sky */
        const br_camera3d_comp *c = (const br_camera3d_comp *)br_get(w, cam_e, ids.camera);
        if (c && c->clear && r->target) {
            br_recti box = br_recti_intersect(viewport, br_recti_make(0, 0, r->target->width, r->target->height));
            int y, x;
            for (y = box.y; y < box.y + box.h; ++y)
                for (x = box.x; x < box.x + box.w; ++x) r->target->pixels[(size_t)y * (size_t)r->target->width + (size_t)x] = c->clear_color;
        }
    }
    if (!br_r3d_begin(r3, r, &cam, viewport)) return false;
    r3->fog = false;
    if (!camera && cam_e != BR_ENTITY_NULL) {                     /* the scene camera's fog */
        const br_camera3d_comp *c = (const br_camera3d_comp *)br_get(w, cam_e, ids.camera);
        if (c && c->fog) {
            r3->fog = true;
            r3->fog_color = c->fog_color;
            r3->fog_start = c->fog_start;
            r3->fog_end = c->fog_end;
        }
    }
    pool = &w->pools[ids.light];                                  /* the lights */
    for (i = 0; i < pool->count; ++i) {
        const br_light *sl = (const br_light *)(pool->data + (size_t)i * pool->stride);
        if (sl->kind == BR_LIGHT_DIRECTIONAL && sl->shadows && shadow_light < 0) { shadow_light = lights; shadow_range = sl->shadow_range; }
        const br_light *l = (const br_light *)(pool->data + (size_t)i * pool->stride);
        br_entity e = pool->entities[i];
        br_light3d L;
        memset(&L, 0, sizeof(L));
        L.kind = (br_light_kind)l->kind;
        L.color = l->color;
        L.intensity = l->intensity;
        L.range = l->range;
        br_world_transform3d(w, e, &L.position, NULL, NULL);
        L.direction = br_has(w, e, ids.transform3d) ? br_forward3d(w, e) : br_v3(0, -1, 0);
        if (br_r3d_add_light(r3, &L)) ++lights;
    }
    if (lights == 0) {                                            /* none: a sun from above, behind the left shoulder */
        br_light3d sun;
        memset(&sun, 0, sizeof(sun));
        sun.kind = BR_LIGHT_DIRECTIONAL;
        sun.direction = br_v3_norm(br_v3(0.35f, -1.0f, -0.55f));
        sun.color = BR_WHITE;
        sun.intensity = 0.9f;
        br_r3d_add_light(r3, &sun);
    }
    if (shadow_light >= 0 && shadow_light < r3->light_count) {   /* real shadows: the casters, seen from the sun */
        br_vec3 ahead = br_quat_rotate(cam.rotation, br_v3(0, 0, -1));
        br_vec3 centre = br_v3_add(cam.position, br_v3_scale(ahead, shadow_range * 0.6f));
        int longest = BR_MAX(viewport.w, viewport.h), size = 256;
        while (size < longest * 3 / 2 && size < 2048) size *= 2;   /* (texels about as fine as the screen's pixels) */
        if (br_r3d_shadow_begin(r3, shadow_light, centre, shadow_range, size)) {
            pool = &w->pools[ids.mesh];
            for (i = 0; i < pool->count; ++i) {
                const br_mesh3d *m = (const br_mesh3d *)(pool->data + (size_t)i * pool->stride);
                const br_mesh *mesh;
                br_entity e = pool->entities[i];
                const br_model *md;
                if (!m->visible || !m->cast_shadow || m->color.a < 128 || !br_has(w, e, ids.transform3d)) continue;
                if ((md = model_of(m)) != NULL) { model_parts(w, r3, e, m, md, NULL); continue; }
                if (!(mesh = br_mesh3d_mesh(m))) continue;
                br_r3d_shadow_mesh(r3, mesh, br_world_matrix3d(w, e));
            }
            br_r3d_shadow_end(r3);
        }
    }
    pool = &w->pools[ids.mesh];
    for (i = 0; i < pool->count; ++i) {                          /* solid things first */
        const br_mesh3d *m = (const br_mesh3d *)(pool->data + (size_t)i * pool->stride);
        br_entity e = pool->entities[i];
        if (!br_has(w, e, ids.transform3d) || !m->visible) continue;
        if (m->color.a < 255) {
            if (!see_through) see_through = (draw3_item *)br_alloc(sizeof(draw3_item) * pool->count);
            if (see_through) {
                br_vec3 p;
                br_world_transform3d(w, e, &p, NULL, NULL);
                see_through[ns].e = e;
                see_through[ns++].dist = br_v3_dist(p, cam.position);
            }
            continue;
        }
        draw_one(w, r3, &ids, e);
    }
    if (!r3->shadows) draw_shadows(w, r3, &ids);                 /* round shadows on what's drawn so far (without real ones) */
    sort_far_first(see_through, ns);                             /* then see-through ones, far to near */
    for (i = 0; i < ns; ++i) draw_one(w, r3, &ids, see_through[i].e);
    br_free(see_through);
    br_particles_draw3d(w, r3);                                  /* last: depth-tested, not depth-written */
    return true;
}

/* Does o + d * t meet the box lo..hi for some t in 0..tmax? */
static bool ray_box(br_vec3 o, br_vec3 d, br_vec3 lo, br_vec3 hi, float tmax) {
    float t0 = 0, t1 = tmax;
    int k;
    for (k = 0; k < 3; ++k) {
        float oc = k == 0 ? o.x : k == 1 ? o.y : o.z, dc = k == 0 ? d.x : k == 1 ? d.y : d.z;
        float a = k == 0 ? lo.x : k == 1 ? lo.y : lo.z, b = k == 0 ? hi.x : k == 1 ? hi.y : hi.z;
        if (fabsf(dc) < 1e-12f) { if (oc < a - 1e-4f || oc > b + 1e-4f) return false; continue; }
        {
            float ta = (a - oc) / dc, tb = (b - oc) / dc;
            if (ta > tb) { float x = ta; ta = tb; tb = x; }
            if (ta > t0) t0 = ta;
            if (tb < t1) t1 = tb;
            if (t0 > t1 + 1e-5f) return false;
        }
    }
    return true;
}

br_entity br_pick3d(const br_world *w, br_vec3 origin, br_vec3 dir, float *t_out, br_vec3 *n_out) {
    br_ids3d ids;
    const br_component_pool *pool;
    br_entity best = BR_ENTITY_NULL;
    float best_t = 1e30f;
    br_vec3 best_n = br_v3(0, 1, 0);
    uint32_t i;
    if (!br_ids3d_of(w, &ids)) return BR_ENTITY_NULL;
    pool = &w->pools[ids.mesh];
    for (i = 0; i < pool->count; ++i) {
        const br_mesh3d *m = (const br_mesh3d *)(pool->data + (size_t)i * pool->stride);
        br_entity e = pool->entities[i];
        const br_mesh *mesh;
        br_mat4 world, inv;
        br_vec3 lo, ld, n;
        float t;
        if (!m->visible || !br_has(w, e, ids.transform3d) || !(mesh = br_mesh3d_mesh(m))) continue;
        world = br_world_matrix3d(w, e);
        inv = br_m4_inverse(world);
        lo = br_m4_mul_point(inv, origin);
        ld = br_m4_mul_dir(inv, dir);                         /* (t is the same along both) */
        if (!ray_box(lo, ld, mesh->min, mesh->max, best_t)) continue;   /* (misses its bounds: no need to look closer) */
        if (br_mesh_raycast(mesh, lo, ld, &t, &n) && t < best_t) {
            best_t = t;
            best = e;
            best_n = br_v3_norm(br_m4_mul_dir(br_m4_transpose(inv), n));
        }
    }
    if (best != BR_ENTITY_NULL) {
        if (t_out) *t_out = best_t;
        if (n_out) *n_out = best_n;
    }
    return best;
}

/* ---- registration ----------------------------------------------------------------------- */

static void sys_draw3d(br_world *w, float dt, void *user) {
    br_engine *e = (br_engine *)w->context;
    BR_UNUSED(dt); BR_UNUSED(user);
    if (!e || !e->renderer.target || !br_world_has_3d(w)) return;
    br_draw3d(w, &e->r3d, &e->renderer, NULL, br_recti_make(0, 0, e->renderer.target->width, e->renderer.target->height));
}

/* ---- Animator ----------------------------------------------------------------------------- */

static const br_model *entity_model(const br_world *w, br_entity e) {
    br_component mc = br_component_find(w, "Mesh");
    return mc != BR_COMPONENT_INVALID ? model_of((const br_mesh3d *)br_get(w, e, mc)) : NULL;
}

float br_animator_clip_length(const br_world *w, br_entity e, const char *clip) {
    const br_model *md = entity_model(w, e);
    int k = md ? br_model_find_clip(md, clip) : -1;
    return k >= 0 ? md->clips[k].duration : 0;
}
int br_animator_clip_count(const br_world *w, br_entity e) { const br_model *md = entity_model(w, e); return md ? md->clip_count : 0; }
const char *br_animator_clip_name(const br_world *w, br_entity e, int i) {
    const br_model *md = entity_model(w, e);
    return md && i >= 0 && i < md->clip_count ? md->clips[i].name : NULL;
}

void br_animator_step(br_world *w, float dt) {
    br_component c = br_component_find(w, "Animator");
    br_component_pool *pool;
    uint32_t i;
    if (c == BR_COMPONENT_INVALID) return;
    pool = &w->pools[c];
    for (i = 0; i < pool->count; ++i) {
        br_animator *a = (br_animator *)(pool->data + (size_t)i * pool->stride);
        float step = dt * a->speed;
        if (strcmp(a->clip, a->started) != 0) {                  /* a new clip: fade the old one out */
            if (a->started[0] && a->blend > 0) {
                memcpy(a->from, a->started, sizeof(a->from));
                a->from_time = a->time;
                a->fade = 0;
                a->fade_len = a->blend;
            } else {
                a->from[0] = 0;
            }
            memcpy(a->started, a->clip, sizeof(a->started));
            a->time = step < 0 ? br_animator_clip_length(w, pool->entities[i], a->clip) : 0;
            a->playing = true;
        }
        if (a->from[0]) {
            a->from_time += step;
            a->fade += dt;
            if (a->fade >= a->fade_len) a->from[0] = 0;
        }
        if (!a->playing) continue;
        a->time += step;
        if (!a->loop) {
            float len = br_animator_clip_length(w, pool->entities[i], a->started);
            if (a->time >= len && step > 0) { a->time = len; a->playing = false; }
            else if (a->time <= 0 && step < 0) { a->time = 0; a->playing = false; }
        }
    }
}

static void animator_added(br_world *w, br_entity e, void *data, void *user) {
    br_animator *a = (br_animator *)data;
    BR_UNUSED(w); BR_UNUSED(e); BR_UNUSED(user);
    a->time = a->from_time = a->fade = a->fade_len = 0;
    a->started[0] = a->from[0] = 0;
}

static void sys_animator(br_world *w, float dt, void *user) { BR_UNUSED(user); br_animator_step(w, dt); }

void br_components3d_register(br_world *w) {
    int i;
    for (i = 0; i < (int)BR_ARRAY_COUNT(k_descs3); ++i) br_component_register_desc(w, &k_descs3[i]);
    br_component_set_hooks(w, br_component_find(w, "Animator"), animator_added, NULL, NULL);
    br_system_add(w, "builtin_animator", BR_PHASE_FIXED_UPDATE, sys_animator, NULL, 26);
    br_system_add(w, "builtin_draw3d", BR_PHASE_RENDER, sys_draw3d, NULL, -10);
}
