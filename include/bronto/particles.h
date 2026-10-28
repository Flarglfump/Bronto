#ifndef BRONTO_PARTICLES_H
#define BRONTO_PARTICLES_H
/* Particles: sparks, smoke, dust, rain, fire and explosions.
 *
 * A Particles component on an entity with a Transform (2D) or a Transform3D
 * (3D) gives off small coloured dots, squares or images that fly, fall, fade
 * and shrink. They are drawn on the entity's `layer` in 2D, and after the
 * meshes (depth-tested against them, fogged with the scene) in 3D.
 *
 *   emitting        gives off `rate` a second while true; turning it on (or the
 *                   game starting with it on) also gives off `burst` at once
 *   rate, burst     a stream (per second), and a puff at once: rate 0 and a
 *                   burst of 40 is an explosion
 *   max             no more than this many alive at once
 *   lifetime        seconds each one lives (`lifetime_random`: up to this much
 *                   shorter, as a fraction: 0.5 is 50% to 100% of it)
 *   speed           how fast they leave (`speed_random`, as a fraction, as above)
 *   angle, spread   2D: the way they go, in degrees (0 right, -90 up, turned with
 *                   the entity), and up to `spread` degrees either side of it.
 *                   3D: they go along the entity's up (+y), within a cone of
 *                   `spread` degrees (180: every way)
 *   area            they start anywhere in this box around the entity (2D: width
 *                   and height; 3D: x and z)
 *   gravity         pulls them down (2D: +y; 3D: -y), in units / second^2;
 *                   negative floats them up (smoke)
 *   drag            slows them, as a fraction of their speed each second
 *   size_start, size_end    across, in pixels (2D) or units (3D), from birth to death
 *   color_start, color_end  likewise (end with alpha 0 to fade them out)
 *   shape           circle, square or image (`image`, tinted by the colour)
 *   blend           alpha, none, add (glowing things) or multiply
 *   layer           2D drawing layer
 *   local           they move with the entity instead of staying where they were
 *                   given off (exhaust stays behind a moving ship; a magic aura
 *                   doesn't)
 *   auto_destroy    the entity is destroyed once it has given off everything it
 *                   will (not emitting, or a rate of 0) and the last one has died:
 *                   for spawned one-off effects
 *
 * Particles only look: they don't collide, and nothing collides with them. They
 * run in the fixed step from their own random numbers (seeded from the entity),
 * so replays draw the same particles every time. */
#include "bronto/ecs.h"
#include "bronto/render.h"

struct br_r3d;

typedef enum br_particle_shape { BR_PARTICLE_CIRCLE = 0, BR_PARTICLE_SQUARE, BR_PARTICLE_IMAGE } br_particle_shape;

typedef struct br_particles {
    bool emitting;
    float rate;
    int burst;
    int max;
    float lifetime, lifetime_random;
    float speed, speed_random;
    float angle, spread;
    br_vec2 area;
    float gravity, drag;
    float size_start, size_end;
    br_color color_start, color_end;
    int shape;                  /* br_particle_shape */
    char image[96];
    int blend;                  /* br_blend_mode */
    int layer;
    bool local;
    bool auto_destroy;
} br_particles;

/* Registers the component and its "builtin_particles" system (fixed update);
 * br_builtin_register does this. */
void br_particles_register(br_world *w);
/* The fixed step: give off, move and age every emitter's particles. */
void br_particles_step(br_world *w, float dt);
/* The editor's preview while a scene isn't playing: as br_particles_step, but
 * nothing is destroyed, and emitters that only burst burst again every so often. */
void br_particles_preview(br_world *w, float dt);
/* Gives off `count` particles at once (count < 0: its `burst`). emit() in scripts. */
bool br_particles_emit(br_world *w, br_entity e, int count);
/* How many of an entity's particles are alive. */
int br_particles_count(const br_world *w, br_entity e);
/* Removes all an entity's particles (or everyone's, with BR_ENTITY_NULL). */
void br_particles_clear(br_world *w, br_entity e);
/* Ready-made looks: "sparks", "fire", "smoke", "explosion" (a burst), "rain", "snow"
 * and "magic", sized for 2D (pixels) or 3D (units). False for an unknown name. */
bool br_particles_preset(br_particles *p, const char *name, bool is3d);
const char *const *br_particles_presets(int *count);
/* Drawing: one 2D emitter (the draw layers call this), and every 3D one after the meshes. */
void br_particles_draw2d(const br_world *w, br_entity e, br_renderer *r);
void br_particles_draw3d(const br_world *w, struct br_r3d *r3);

#endif
