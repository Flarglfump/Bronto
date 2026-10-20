/*
 * Bronto Engine - 2D rigid-body physics.
 *
 * Components (registered by br_builtin_register, editable in the editor):
 *
 *   RigidBody     makes an entity move under physics: dynamic (pushed by
 *                 gravity and collisions), static (never moves) or
 *                 kinematic (moves only by its own velocity, pushes others)
 *   Collider      its shape: a box or a circle, centred on the Transform
 *                 (plus an offset), scaled with it. Friction, bounce, and
 *                 `trigger` (reports overlaps but doesn't collide).
 *                 A Collider without a RigidBody is static scenery.
 *   PhysicsWorld  optional, on any one entity: gravity and solver iterations
 *
 * Units are the scene's: pixels, seconds and degrees, with +y pointing
 * down, so the default gravity (0, 600) pulls things down the screen.
 *
 * The "builtin_physics" system steps everything in the fixed update: it
 * integrates velocities, finds overlapping pairs (sort and sweep), builds
 * contact points (circle and oriented box, with face clipping), and solves
 * them with sequential impulses, warm-started from the previous step so
 * stacks settle. Positions and rotations are written back to Transform.
 *
 * Collision layers: a collider is on one `layer` (0-31) and collides with
 * the layers whose bits are set in its `mask` (-1 = all). Both sides must
 * agree.
 *
 * After each step, br_physics_events() lists what began and ended touching
 * (and trigger enters/exits); scripts receive them as on_collision /
 * on_trigger calls.
 */
#ifndef BRONTO_PHYSICS_H
#define BRONTO_PHYSICS_H

#include "ecs.h"
#include "render.h"

typedef enum br_body_type { BR_BODY_DYNAMIC = 0, BR_BODY_STATIC, BR_BODY_KINEMATIC } br_body_type;
typedef enum br_collider_shape { BR_COLLIDER_BOX = 0, BR_COLLIDER_CIRCLE } br_collider_shape;

typedef struct br_rigidbody {
    int type;                  /* br_body_type */
    float mass;                /* > 0 */
    float gravity_scale;
    br_vec2 velocity;          /* pixels / second */
    float angular_velocity;    /* degrees / second, clockwise */
    float linear_damping;      /* per second, 0 = none */
    float angular_damping;
    bool fixed_rotation;
    /* runtime only (not saved): forces gathered for the next step */
    br_vec2 force;
    float torque;
} br_rigidbody;

typedef struct br_collider {
    int shape;                 /* br_collider_shape */
    br_vec2 size;              /* box width, height */
    float radius;              /* circle */
    br_vec2 offset;            /* from the Transform's position, rotating with it */
    float friction;            /* 0 = ice .. 1 = rubber */
    float bounce;              /* restitution: 0 = no bounce .. 1 = no loss */
    bool trigger;              /* overlaps are reported, nothing collides */
    int layer;                 /* 0..31 */
    int mask;                  /* layers it collides with (bits), -1 = all */
} br_collider;

typedef struct br_physics_world {
    br_vec2 gravity;           /* pixels / second^2 */
    int iterations;            /* solver passes per step (more = stiffer stacks) */
} br_physics_world;

typedef enum br_contact_kind {
    BR_CONTACT_BEGIN = 0,      /* two colliders started touching */
    BR_CONTACT_END,            /* ... stopped */
    BR_TRIGGER_ENTER,          /* a collider entered a trigger (either side may be the trigger) */
    BR_TRIGGER_EXIT
} br_contact_kind;

typedef struct br_contact_event {
    br_contact_kind kind;
    br_entity a, b;
    br_vec2 normal;            /* from a towards b (BEGIN only) */
    br_vec2 point;             /* a contact point (BEGIN only) */
    float speed;               /* how fast they approached along the normal (BEGIN only) */
} br_contact_event;

typedef struct br_raycast_hit {
    br_entity entity;
    br_vec2 point, normal;
    float fraction;            /* 0 at `from` .. 1 at `to` */
} br_raycast_hit;

/* Registers the components and the system (br_builtin_register does this). */
void br_physics_register(br_world *w);
const br_component_desc *const *br_physics_descs(int *count);

/* One physics step (what the system runs every fixed update). */
void br_physics_step(br_world *w, float dt);

/* Events from the most recent step; valid until the next one. */
const br_contact_event *br_physics_events(const br_world *w, uint32_t *count);

/* Pushes: a force acts over the next step; an impulse changes velocity now.
 * `point` is in world space (NULL: the body's centre, no spin). */
void br_physics_apply_force(br_world *w, br_entity e, br_vec2 force, const br_vec2 *point);
void br_physics_apply_impulse(br_world *w, br_entity e, br_vec2 impulse, const br_vec2 *point);

/* The first non-trigger collider along the segment whose layer is in
 * `mask` (-1 = all), ignoring `ignore`. */
bool br_physics_raycast(const br_world *w, br_vec2 from, br_vec2 to, int mask, br_entity ignore, br_raycast_hit *hit);
/* Is any collider (triggers included) under this world point? Returns it, or BR_ENTITY_NULL. */
br_entity br_physics_point(const br_world *w, br_vec2 point, int mask);

/* Collider outlines in world space (draw with the scene's camera). */
void br_physics_debug_draw(const br_world *w, br_renderer *r, br_color solid, br_color trigger);

/* The gravity in effect (the PhysicsWorld component's, or the default). */
br_vec2 br_physics_gravity(const br_world *w);

#endif /* BRONTO_PHYSICS_H */
