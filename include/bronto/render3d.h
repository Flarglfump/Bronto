/*
 * Bronto Engine - 3D rendering.
 *
 * Meshes are drawn in software into the same RGBA framebuffer as the 2D
 * calls (render.h), with a depth buffer, perspective-correct texturing and
 * per-vertex (Gouraud) lighting. Triangles are clipped against the near
 * plane, back faces are culled, and pixel coverage uses the same fixed
 * point and top-left rule as the 2D rasterizer, so shared edges have no
 * cracks or double pixels. Everything is deterministic.
 *
 * Conventions: right-handed, +Y up, the camera looks down -Z; front faces
 * wind counter-clockwise; the generated primitives are 1 unit across and
 * centred on the origin (a Transform3D's scale sizes them).
 *
 *   br_r3d r3;  br_r3d_init(&r3);
 *   br_r3d_begin(&r3, &engine->renderer, &camera, viewport);   (clears depth)
 *   br_r3d_add_light(&r3, &sun);
 *   br_r3d_draw_mesh(&r3, &cube, model_matrix, &material);
 *   br_r3d_line(&r3, a, b, BR_RED, true);
 *
 * Scenes made of components (Transform3D, Mesh, Camera, Light) are drawn
 * by components3d.h; this file is the layer underneath.
 */
#ifndef BRONTO_RENDER3D_H
#define BRONTO_RENDER3D_H

#include "render.h"

/* ---- meshes --------------------------------------------------------------- */
typedef struct br_vertex3d { br_vec3 pos, normal; br_vec2 uv; } br_vertex3d;

typedef struct br_mesh {
    br_vertex3d *verts;
    uint32_t vert_count;
    uint32_t *indices;          /* three per triangle, counter-clockwise from the front */
    uint32_t index_count;
    br_vec3 min, max;           /* bounds */
    struct br_mesh_bvh *bvh;    /* its triangles sorted into nested boxes (made when needed; NULL: not yet) */
} br_mesh;

void br_mesh_destroy(br_mesh *m);
bool br_mesh_init(br_mesh *m, uint32_t verts, uint32_t indices);   /* room for them, counts set */
void br_mesh_bounds(br_mesh *m);                                   /* recomputes min / max */
/* Primitives, 1 unit across, centred on the origin. */
bool br_mesh_cube(br_mesh *m);
bool br_mesh_sphere(br_mesh *m, int segments, int rings);          /* diameter 1 */
bool br_mesh_plane(br_mesh *m);                                    /* 1 x 1 in X and Z, facing +Y */
bool br_mesh_cylinder(br_mesh *m, int segments);                   /* diameter 1, height 1, along Y */
bool br_mesh_cone(br_mesh *m, int segments);                       /* base diameter 1, height 1, point up */
bool br_mesh_capsule(br_mesh *m, int segments, int rings);         /* 1 across, 2 tall: a character (Collider3D capsule, size.y 2) */
/* Wavefront .obj: v, vt, vn and f (polygons are fanned into triangles,
 * negative indices work); materials, groups and lines are skipped. Without
 * normals, each face gets its flat normal. `err` says what was wrong. */
bool br_mesh_load_obj_mem(br_mesh *m, const char *text, size_t size, char *err, size_t err_size);
bool br_mesh_load_obj(br_mesh *m, const char *path, char *err, size_t err_size);
/* The nearest hit of a ray (origin, direction, both in the mesh's space)
 * on the mesh's front or back faces: distance along `dir` (in units of
 * |dir|) and the face normal. */
bool br_mesh_raycast(const br_mesh *m, br_vec3 origin, br_vec3 dir, float *t, br_vec3 *normal);
/* A bounding volume hierarchy over the triangles, so rays and boxes only look
 * at the few triangles near them: raycasts and mesh colliders make it by
 * themselves for meshes of more than a few dozen triangles. Changing the
 * vertices means calling br_mesh_bounds (which drops it) afterwards. */
bool br_mesh_build_bvh(br_mesh *m);
void br_mesh_drop_bvh(br_mesh *m);
/* The triangles (their number: indices[3t..3t+2]) whose bounds touch the box
 * lo..hi (mesh space), at most `max` of them; returns how many there are (which
 * can be more than `max`: then only the first `max` were written). */
uint32_t br_mesh_triangles_in(const br_mesh *m, br_vec3 lo, br_vec3 hi, uint32_t *out, uint32_t max);

/* ---- camera, lights, materials ---------------------------------------------- */
typedef struct br_camera3d {
    br_vec3 position;
    br_quat rotation;           /* identity: looking down -Z, +Y up */
    bool orthographic;
    float fov;                  /* vertical, degrees (perspective) */
    float size;                 /* half the view's height in units (orthographic) */
    float near_clip, far_clip;
} br_camera3d;
br_camera3d br_camera3d_make(br_vec3 position, br_vec3 target);   /* 60 degrees, 0.1 .. 1000 */
br_mat4 br_camera3d_view(const br_camera3d *c);
br_mat4 br_camera3d_projection(const br_camera3d *c, float aspect);

typedef enum br_light_kind { BR_LIGHT_DIRECTIONAL = 0, BR_LIGHT_POINT, BR_LIGHT_AMBIENT } br_light_kind;
typedef struct br_light3d {
    br_light_kind kind;
    br_vec3 position;           /* point */
    br_vec3 direction;          /* directional: the way the light travels */
    br_color color;
    float intensity;
    float range;                /* point: no light beyond this */
} br_light3d;

typedef struct br_material {
    br_color color;             /* multiplies the texture; alpha < 255 is see-through */
    const br_image *texture;    /* NULL: plain colour */
    bool lit;                   /* false: full brightness, no lights */
    bool cull;                  /* hide back faces */
    br_vec2 uv_scale;           /* texture repeats (1, 1: once) */
} br_material;
br_material br_material_make(br_color color);

/* ---- the 3D pass ------------------------------------------------------------------ */
#define BR_R3D_MAX_LIGHTS 8
typedef struct br_r3d_stats { uint32_t meshes, triangles, culled, pixels; } br_r3d_stats;

typedef struct br_r3d {
    br_renderer *r;
    float *depth;               /* one per framebuffer pixel: 0 near .. 1 far */
    int depth_w, depth_h;
    br_recti viewport;
    br_camera3d camera;
    br_mat4 view, proj, view_proj;
    br_light3d lights[BR_R3D_MAX_LIGHTS];
    int light_count;
    br_color ambient;           /* added to every lit surface (default dark grey) */
    bool fog;                   /* fade with distance into fog_color, from fog_start to fog_end units */
    br_color fog_color;
    float fog_start, fog_end;
    /* shadows from one directional light (br_r3d_shadow_*) */
    bool shadows;               /* on for the meshes drawn after br_r3d_shadow_end */
    int shadow_light;           /* which of `lights` casts them */
    float *shadow_depth;        /* the light's view of the scene: depth per texel */
    int shadow_size;
    br_mat4 shadow_vp;          /* world -> the light's clip space */
    float shadow_texel;         /* one texel, in units */
    float shadow_strength;      /* 1: the light is blocked completely */
    br_r3d_stats stats;
} br_r3d;

void br_r3d_init(br_r3d *r3);
void br_r3d_destroy(br_r3d *r3);
/* Starts drawing through `camera` into `viewport` of the renderer's target
 * (the camera's aspect is the viewport's). Clears the depth there and the
 * lights. False if the target is missing or memory runs out. */
bool br_r3d_begin(br_r3d *r3, br_renderer *r, const br_camera3d *camera, br_recti viewport);
bool br_r3d_add_light(br_r3d *r3, const br_light3d *l);
void br_r3d_draw_mesh(br_r3d *r3, const br_mesh *m, br_mat4 model, const br_material *mat);
/* Real shadows from a directional light (one of those added, `light`): after
 * the lights, begin, draw every mesh that casts a shadow with br_r3d_shadow_mesh,
 * end; the lit meshes drawn after that are shadowed (per pixel, softened
 * over four texels). The shadow map covers `radius` units around `centre`, at
 * `size` x `size` texels. br_r3d_begin turns shadows off again. */
bool br_r3d_shadow_begin(br_r3d *r3, int light, br_vec3 centre, float radius, int size);
void br_r3d_shadow_mesh(br_r3d *r3, const br_mesh *m, br_mat4 model);
void br_r3d_shadow_end(br_r3d *r3);
/* A line in the world: with depth_test, hidden behind what is nearer. */
void br_r3d_line(br_r3d *r3, br_vec3 a, br_vec3 b, br_color c, bool depth_test);
/* A world point on the screen: x, y in pixels, z the depth (0..1). False
 * behind the camera. */
bool br_r3d_project(const br_r3d *r3, br_vec3 p, br_vec3 *screen);
/* The ray through a screen point (pixels): from the near plane, unit direction. */
void br_r3d_ray(const br_r3d *r3, br_vec2 screen, br_vec3 *origin, br_vec3 *dir);

#endif /* BRONTO_RENDER3D_H */
