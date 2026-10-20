/*
 * Bronto Engine - built-in components.
 *
 * A small set of described components (ecs.h), and the systems that bring
 * them to life. They are enough to build simple scenes in the editor, save
 * them, and play them in any game:
 *
 *   Name       what the editor's hierarchy calls the entity
 *   Transform  position, rotation (degrees, clockwise) and scale
 *   Shape      a rectangle, ellipse or triangle with a fill and an outline
 *   Sprite     an image (BMP or TGA, through the asset cache), tinted
 *   Label      a line of text
 *   Motion     velocity (units per second) and spin (degrees per second)
 *   Bounds     keeps a moving entity inside a box: bounce off the edges, or wrap
 *   (physics.h adds RigidBody, Collider and PhysicsWorld; script.h adds Script)
 *   Sound      a WAV file (through the asset cache) played on start, or on
 *              request, once or looping; stopped when the entity goes away
 *
 * Shapes, sprites and labels are drawn centred on the transform's position,
 * lowest `layer` first. Games add their own described components next to
 * these the same way.
 */
#ifndef BRONTO_COMPONENTS_H
#define BRONTO_COMPONENTS_H

#include "ecs.h"
#include "render.h"
#include "audio.h"

typedef enum br_shape_kind { BR_SHAPE_RECT = 0, BR_SHAPE_ELLIPSE, BR_SHAPE_TRIANGLE } br_shape_kind;

typedef struct br_name      { char text[32]; } br_name;
typedef struct br_transform { br_vec2 position; float rotation; br_vec2 scale; } br_transform;
typedef struct br_shape     { int kind; br_vec2 size; br_color color; br_color outline; int layer; } br_shape;
typedef struct br_sprite    { char image[96]; br_color tint; int layer; } br_sprite;
typedef struct br_label     { char text[64]; float size; br_color color; int layer; } br_label;
typedef struct br_motion    { br_vec2 velocity; float spin; } br_motion;
typedef struct br_bounds    { br_vec2 min, max; bool wrap; } br_bounds;
typedef struct br_sound_src {
    char file[96]; float volume; bool loop; bool play_on_start;
    br_voice voice;             /* runtime only: not saved, reset when added */
    bool started;
} br_sound_src;

typedef struct br_builtin_ids {
    br_component name, transform, shape, sprite, label, motion, bounds, sound;
} br_builtin_ids;

/* Registers every built-in component and three systems: "builtin_motion"
 * and "builtin_audio" (fixed update) and "builtin_draw" (render). Drawing
 * and sound use the engine in world->context (nothing happens while it is
 * NULL). `ids` may be NULL. */
void br_builtin_register(br_world *w, br_builtin_ids *ids);
/* The ids of a world where they were registered (false if they weren't). */
bool br_builtin_ids_of(const br_world *w, br_builtin_ids *ids);
/* All built-in descriptions, in registration order. */
const br_component_desc *const *br_builtin_descs(int *count);

/* What the systems do, callable directly (the editor draws with its own camera). */
void br_builtin_step(br_world *w, float dt);
void br_builtin_draw(const br_world *w, br_renderer *r);

/* Starts (or restarts) / stops an entity's Sound through the engine's mixer
 * in world->context. Playing returns false without a Sound, an engine, or a
 * loadable file. */
bool br_builtin_sound_play(br_world *w, br_entity e);
void br_builtin_sound_stop(br_world *w, br_entity e);

/* Geometry for tools: the entity's visible box as 4 world-space corners
 * (false without a Transform), and whether a world point is inside it. */
bool br_builtin_box(const br_world *w, br_entity e, br_vec2 corners[4]);
bool br_builtin_hit(const br_world *w, br_entity e, br_vec2 point);

/* Frees the draw list's scratch memory (the engine calls this at shutdown). */
void br_builtin_release(void);

#endif /* BRONTO_COMPONENTS_H */
