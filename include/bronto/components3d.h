/*
 * Bronto Engine - 3D components.
 *
 * A 3D entity has a Transform3D (instead of the 2D Transform), and is drawn
 * through the scene's Camera3D if it has a Mesh, lit by its Lights:
 *
 *   Transform3D { position, rotation, scale }   vec3s; rotation is Euler
 *                 angles in degrees (bmath.h: pitch x, yaw y, roll z)
 *   Mesh        { shape, file, texture, color, lit, cull, uv_scale, visible }
 *                 shape: cube, sphere, plane, cylinder, cone (1 unit
 *                 across: scale sizes them) or model (an .obj `file`)
 *   Camera3D    { projection, fov, size, near, far, active, priority,
 *                 clear, clear_color }   it looks down its own -Z (the 2D
 *                 Camera of components.h is a different thing)
 *   Light       { kind, color, intensity, range }   directional lights shine
 *                 down their -Z; point lights from where they are; ambient
 *                 lights everywhere
 *   Animator    { clip, speed, loop, playing, blend }   plays the animations of
 *                 its Mesh's glTF model (model.h), crossfading over `blend`
 *                 seconds when `clip` changes
 *
 * Parents work as in 2D (hierarchy.h): a child's Transform3D is relative
 * to its nearest ancestor with a Transform3D. The engine draws 3D before
 * 2D, so Sprites, Shapes and Labels without a Transform3D sit on top (a
 * HUD). Physics in 3D: physics3d.h.
 */
#ifndef BRONTO_COMPONENTS3D_H
#define BRONTO_COMPONENTS3D_H

#include "ecs.h"
#include "render3d.h"

typedef struct br_transform3d { br_vec3 position, rotation, scale; } br_transform3d;

typedef enum br_mesh_shape {
    BR_MESH_CUBE = 0, BR_MESH_SPHERE, BR_MESH_PLANE, BR_MESH_CYLINDER, BR_MESH_CONE, BR_MESH_CAPSULE, BR_MESH_MODEL
} br_mesh_shape;
typedef struct br_mesh3d {
    int shape;
    char file[96];              /* BR_MESH_MODEL: an .obj in the project */
    char texture[96];           /* an image wrapped on it ("" = none) */
    br_color color;
    bool lit, cull, visible;
    br_vec2 uv_scale;
    bool shadow;                /* a soft round shadow on whatever is below it (when no Light casts real ones) */
    bool cast_shadow;           /* blocks a Light's real shadows (lights with `shadows` on) */
} br_mesh3d;

typedef enum br_projection { BR_PROJECTION_PERSPECTIVE = 0, BR_PROJECTION_ORTHOGRAPHIC } br_projection;
typedef struct br_camera3d_comp {
    int projection;
    float fov;                  /* degrees, top to bottom */
    float size;                 /* orthographic: half the view's height, in units */
    float near_clip, far_clip;
    bool active;
    int priority;               /* of the active cameras, the highest draws */
    bool clear;                 /* fill the screen with clear_color first */
    br_color clear_color;
    bool fog;                   /* things fade into fog_color from fog_start to fog_end units away */
    br_color fog_color;
    float fog_start, fog_end;
} br_camera3d_comp;

typedef struct br_light {
    int kind;                   /* br_light_kind: directional, point, ambient */
    br_color color;
    float intensity;
    float range;                /* point */
    bool shadows;               /* directional: real shadows, from every Mesh with cast_shadow */
    float shadow_range;         /* ... over this far around what the camera looks at (units) */
} br_light;

/* Animator: plays one of the animations of a glTF model (its Mesh's file).
 * Setting `clip` to another name starts that one, faded in over `blend`
 * seconds; without loop it stops at its end (and `playing` turns false). */
typedef struct br_animator {
    char clip[48];
    float speed;                /* 1: as made; 2: twice as fast; negative: backwards */
    bool loop, playing;
    float blend;                /* seconds to crossfade into a new clip */
    /* runtime only (not saved) */
    float time;                 /* seconds into `clip` */
    char started[48];           /* the clip `time` belongs to */
    char from[48];              /* the clip fading out ... */
    float from_time;            /* ... where it is ... */
    float fade, fade_len;       /* ... and how far through the fade */
} br_animator;

typedef struct br_ids3d { br_component transform3d, mesh, camera, light; } br_ids3d;

/* Registers the four (br_builtin_register does this) and the 3D draw
 * system: it draws the scene through its camera before the 2D draw. */
void br_components3d_register(br_world *w);
bool br_ids3d_of(const br_world *w, br_ids3d *ids);
const br_component_desc *const *br_components3d_descs(int *count);

/* ---- transforms ------------------------------------------------------------- */
br_mat4 br_transform3d_matrix(const br_transform3d *t);
/* Where an entity is in the world: its Transform3D after its ancestors'.
 * Without a Transform3D, the identity. */
br_mat4 br_world_matrix3d(const br_world *w, br_entity e);
void    br_world_transform3d(const br_world *w, br_entity e, br_vec3 *position, br_quat *rotation, br_vec3 *scale);
/* Moves / turns / places it (its local Transform3D follows its parent). */
void    br_set_world_position3d(br_world *w, br_entity e, br_vec3 position);
void    br_set_world_rotation3d(br_world *w, br_entity e, br_quat rotation);
void    br_set_world_matrix3d(br_world *w, br_entity e, br_mat4 world);
/* Its -Z, +X and +Y in the world (unit length). */
br_vec3 br_forward3d(const br_world *w, br_entity e);
br_vec3 br_right3d(const br_world *w, br_entity e);
br_vec3 br_up3d(const br_world *w, br_entity e);
/* Turns it to face a world point (its -Z toward it, +Y kept up). */
void    br_look_at3d(br_world *w, br_entity e, br_vec3 target);

/* ---- drawing ------------------------------------------------------------------ */
/* The mesh a Mesh component shows (a shared primitive, or the cached model);
 * NULL if its model can't be loaded. */
const br_mesh *br_mesh3d_mesh(const br_mesh3d *m);
/* The scene's camera: the active Camera3D with a Transform3D and the highest
 * priority. False if there is none. */
bool br_scene_camera3d(const br_world *w, br_camera3d *out, br_entity *which);
/* Draws every visible Mesh through `camera` (NULL: the scene's) into
 * `viewport`, lit by the scene's Lights (with none, a default sun). Clears
 * the depth there, and the colour too when the scene camera asks.
 * Returns false (drawing nothing) without a camera. */
bool br_draw3d(const br_world *w, br_r3d *r3, br_renderer *r, const br_camera3d *camera, br_recti viewport);
/* The Mesh entity a world ray hits first (BR_ENTITY_NULL: none), the
 * distance along `dir` (unit), and the hit point's normal. */
br_entity br_pick3d(const br_world *w, br_vec3 origin, br_vec3 dir, float *t, br_vec3 *normal);
/* Does the world have anything for the 3D pass (a Transform3D)? */
bool br_world_has_3d(const br_world *w);

/* Moves every Animator on by dt (the fixed step does this; the editor too, to
 * preview). Clip names with no animation in the model just hold the rest pose. */
void br_animator_step(br_world *w, float dt);
/* The clips an entity's model has (NULL, 0: none), and how long one lasts. */
int br_animator_clip_count(const br_world *w, br_entity e);
const char *br_animator_clip_name(const br_world *w, br_entity e, int index);
float br_animator_clip_length(const br_world *w, br_entity e, const char *clip);

/* Frees the shared primitive meshes (the engine calls this at shutdown). */
void br_components3d_release(void);

#endif /* BRONTO_COMPONENTS3D_H */
