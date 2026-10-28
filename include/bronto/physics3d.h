/*
 * Bronto Engine - 3D rigid-body physics.
 *
 * The 3D counterpart of physics.h, for entities with a Transform3D:
 *
 *   RigidBody3D     dynamic (gravity and collisions move it), static (never
 *                   moves) or kinematic (moves by its own velocity only,
 *                   pushes dynamic bodies). Velocity in units / second,
 *                   angular velocity in degrees / second about each axis.
 *                   lock_rotation keeps it upright (a character).
 *   Collider3D      a box (`size`: full width, height, depth), a sphere
 *                   (`radius`), a capsule (`radius` and `size.y`, its full
 *                   height: upright, for characters) or a mesh (the
 *                   triangles of the entity's Mesh: terrain, ramps, models;
 *                   static or kinematic only), centred on the Transform3D
 *                   plus `offset`, turned and scaled with it. Friction, bounce, trigger,
 *                   layer and mask as in 2D. Without a RigidBody3D it is
 *                   static scenery.
 *   PhysicsWorld3D  optional: gravity (default 0, -9.81, 0: metres) and
 *                   solver iterations.
 *
 * Each fixed step integrates forces, finds pairs (sort and sweep on the
 * bounding boxes), builds contact manifolds (sphere-sphere, sphere-box,
 * and box-box by separating axes with the incident face clipped against
 * the reference face: up to four points, recognised again next step by
 * their features), warm-starts and solves them with sequential impulses
 * (normal, two friction directions, restitution, Baumgarte position
 * correction), integrates and writes the Transform3Ds back.
 *
 * Events (begin / end touching, trigger enter / exit) go to scripts'
 * on_collision / on_collision_end / on_trigger / on_trigger_exit, with a
 * vec3 normal.
 */
#ifndef BRONTO_PHYSICS3D_H
#define BRONTO_PHYSICS3D_H

#include "ecs.h"
#include "physics.h"
#include "render3d.h"

typedef enum br_collider3d_shape {
    BR_COLLIDER3D_BOX = 0, BR_COLLIDER3D_SPHERE,
    BR_COLLIDER3D_CAPSULE,     /* `radius` round a line along its own y; `size.y` the full height, end to end */
    BR_COLLIDER3D_MESH         /* the triangles of the entity's Mesh: for scenery (it never falls) */
} br_collider3d_shape;

typedef struct br_rigidbody3d {
    int type;                  /* br_body_type: dynamic, static, kinematic */
    float mass;
    float gravity_scale;
    br_vec3 velocity;
    br_vec3 angular_velocity;  /* degrees / second about x, y, z (world) */
    float linear_damping, angular_damping;
    bool lock_rotation;
    /* runtime only: forces gathered for the next step */
    br_vec3 force, torque;
} br_rigidbody3d;

typedef struct br_collider3d {
    int shape;                 /* br_collider3d_shape */
    br_vec3 size;              /* box: full extents */
    float radius;              /* sphere */
    br_vec3 offset;
    float friction, bounce;
    bool trigger;
    int layer, mask;
} br_collider3d;

typedef struct br_physics_world3d { br_vec3 gravity; int iterations; } br_physics_world3d;

typedef struct br_contact3d_event {
    br_contact_kind kind;      /* begin, end, trigger enter, trigger exit (physics.h) */
    br_entity a, b;
    br_vec3 normal;            /* from a towards b (begin only) */
    br_vec3 point;
    float speed;               /* how fast they met along the normal (begin only) */
} br_contact3d_event;

typedef struct br_raycast3d_hit {
    br_entity entity;
    br_vec3 point, normal;
    float distance;            /* from the ray's start */
} br_raycast3d_hit;

void br_physics3d_register(br_world *w);
const br_component_desc *const *br_physics3d_descs(int *count);
void br_physics3d_step(br_world *w, float dt);
const br_contact3d_event *br_physics3d_events(const br_world *w, uint32_t *count);
br_vec3 br_physics3d_gravity(const br_world *w);

/* `point` in the world (NULL: the centre, no spin). */
void br_physics3d_apply_force(br_world *w, br_entity e, br_vec3 force, const br_vec3 *point);
void br_physics3d_apply_impulse(br_world *w, br_entity e, br_vec3 impulse, const br_vec3 *point);
void br_physics3d_apply_torque(br_world *w, br_entity e, br_vec3 torque);
/* The first non-trigger collider along the segment from `from` to `to`
 * whose layer is in `mask` (-1: all), ignoring `ignore`. */
bool br_physics3d_raycast(const br_world *w, br_vec3 from, br_vec3 to, int mask, br_entity ignore, br_raycast3d_hit *hit);
/* Colliders (triggers too) overlapping a sphere: how many (fills up to max). */
uint32_t br_physics3d_overlap_sphere(const br_world *w, br_vec3 centre, float radius, int mask, br_entity *out, uint32_t max);
/* Moving a character: br_physics3d_move moves an entity's collider (a
 * capsule, sphere or box) by `motion`, sliding along whatever solid
 * colliders it meets instead of going into them, and stepping it in small
 * pieces so fast moves don't pass through walls. Gravity, jumping and speed
 * are up to the caller (a kinematic RigidBody3D, or none at all).
 * `velocity`, if given, has the parts that pushed into things taken out. */
typedef struct br_physics3d_move_info {
    br_entity entity;
    bool on_floor, on_wall, on_ceiling;
    br_vec3 floor_normal;
} br_physics3d_move_info;
bool br_physics3d_move(br_world *w, br_entity e, br_vec3 motion, br_vec3 *velocity, br_physics3d_move_info *info);
/* What the last br_physics3d_move of this entity found (false: it hasn't moved). */
bool br_physics3d_last_move(const br_world *w, br_entity e, br_physics3d_move_info *info);

/* Collider outlines as lines (call between br_r3d_begin and drawing). */
void br_physics3d_debug_draw(const br_world *w, br_r3d *r3, br_color solid, br_color trigger);

#endif /* BRONTO_PHYSICS3D_H */
