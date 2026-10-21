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
 *   Tilemap    a grid of tiles from one image (a tileset), one character per
 *              cell; some characters can be marked solid for scripts to test
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
/* Tilemap: `columns` x `rows` cells of tile_size pixels, drawn centred on
 * the Transform like everything else (so its top-left cell is at
 * -columns * tile_size / 2, -rows * tile_size / 2). `tiles` has one
 * character per cell, row by row. A character's position in `legend` is
 * its tile in the tileset image (tiles numbered left to right, then top to
 * bottom); characters not in the legend (and missing cells) show nothing.
 * `solid` lists the characters br_tilemap_solid (tile_solid() in scripts)
 * reports as blocking. */
#define BR_TILEMAP_MAX 2048
typedef struct br_tilemap {
    char tileset[96];
    int tile_size;
    int columns, rows;
    char legend[96];
    char solid[64];
    char tiles[BR_TILEMAP_MAX + 1];
    int layer;
    br_color tint;
} br_tilemap;

/* Tags: words that say what an entity is ("enemy", "pickup", "flying"), for
 * finding entities by kind rather than by name. `list` holds them separated
 * by commas or spaces ("enemy, flying"); a tag is up to BR_TAG_MAX - 1
 * letters, digits and _-.:/ characters, and matching is exact (case counts).
 * Edit the list in the inspector, or use the br_tag_* functions (has_tag(),
 * add_tag(), find_tagged()... in scripts). */
#define BR_TAG_MAX 32
#define BR_TAGS_MAX 128
typedef struct br_tags { char list[BR_TAGS_MAX]; } br_tags;

typedef struct br_sound_src {
    char file[96]; float volume; bool loop; bool play_on_start;
    int bus;                    /* BR_BUS_* it plays on: sfx (default), music, ui, ambient */
    br_voice voice;             /* runtime only: not saved, reset when added */
    bool started;
} br_sound_src;

typedef struct br_builtin_ids {
    br_component name, transform, shape, sprite, label, motion, bounds, sound, tilemap, tags;
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

/* Tilemaps, by world position (they follow the map's Transform and its
 * parents). The cell under `point`: its column and row (either may be
 * NULL), and the character there - 0 when the point is outside the map. */
char br_tilemap_get(const br_world *w, br_entity map, br_vec2 point, int *col, int *row);
/* Sets the cell under `point`; false outside the map or without a Tilemap. */
bool br_tilemap_set(br_world *w, br_entity map, br_vec2 point, char c);
/* Is the cell under `point` one of the map's solid characters? (Outside: no.) */
bool br_tilemap_solid(const br_world *w, br_entity map, br_vec2 point);
/* The world position of the centre of a cell (any column and row, even
 * outside the map). False without a Tilemap. */
bool br_tilemap_cell_center(const br_world *w, br_entity map, int col, int row, br_vec2 *out);

/* ---- tags (the Tags component) -------------------------------------------- */
/* Does the entity have this tag? */
bool br_tag_has(const br_world *w, br_entity e, const char *tag);
/* Gives it the tag (adding a Tags component if it has none). True if it has it
 * now; false for a tag that isn't a valid one or doesn't fit. */
bool br_tag_add(br_world *w, br_entity e, const char *tag);
/* Takes the tag off; true if it had it. */
bool br_tag_remove(br_world *w, br_entity e, const char *tag);
/* Its tags, one by one: the index'th into `out` (false past the last). */
bool br_tag_at(const br_world *w, br_entity e, int index, char *out, size_t size);
int br_tag_count(const br_world *w, br_entity e);
/* Is `tag` a tag that can be given (1 to BR_TAG_MAX - 1 letters, digits, _-.:/)? */
bool br_tag_valid(const char *tag);
/* The entities with every one of `tags` (count of them), into `out` (up to
 * `max`; NULL to count). Returns how many there are, which may be more than max.
 * In the order they got their Tags. */
uint32_t br_find_tagged(const br_world *w, const char *const *tags, int tag_count, br_entity *out, uint32_t max);

/* Frees the draw list's scratch memory (the engine calls this at shutdown). */
void br_builtin_release(void);

#endif /* BRONTO_COMPONENTS_H */
