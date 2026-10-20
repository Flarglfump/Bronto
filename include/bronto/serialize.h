/*
 * Bronto Engine - scene files.
 *
 * Saves and loads the entities of an ECS world as readable, diff-friendly
 * text. Every component registered with a description (ecs.h) is written
 * field by field; undescribed components are runtime-only and skipped.
 *
 *   # Bronto scene
 *   bronto_scene 1
 *
 *   entity
 *     Name
 *       text = "Player"
 *     Transform
 *       position = 120 80
 *       rotation = 0
 *       scale = 1 1
 *     Shape
 *       kind = circle
 *       color = #ff7828ff
 *
 * Values: int and float numbers, true/false, "x y" pairs, #rrggbbaa
 * colours, "quoted strings" (with \" \\ \n escapes) and enum names.
 * Indentation is optional and '#' starts a comment. Loading is forgiving
 * about what it doesn't know: an unknown component or field (say, from a
 * newer game) is skipped with a warning, and fields a file leaves out keep
 * their defaults. A malformed value is an error, and then nothing is loaded.
 */
#ifndef BRONTO_SERIALIZE_H
#define BRONTO_SERIALIZE_H

#include "ecs.h"

#define BR_SCENE_FORMAT_VERSION 1

/* Writes `count` entities in the order given (order == NULL: every live
 * entity, in index order). Returns br_alloc'd text; free with br_free. */
char *br_world_to_text(const br_world *w, const br_entity *order, uint32_t count, size_t *out_len);

/* Creates the entities described by `text` in the world, whose components
 * must already be registered. Their handles are written to `out` (may be
 * NULL) in file order, up to `cap`. Returns the number of entities created,
 * or -1 on an error, with a message ("line 12: bad colour ...") in `err`
 * (may be NULL). On an error the world is left unchanged. */
int br_world_from_text(br_world *w, const char *text, br_entity *out, uint32_t cap,
                       char *err, size_t err_size);

bool br_world_save_file(const br_world *w, const br_entity *order, uint32_t count, const char *path);
int  br_world_load_file(br_world *w, const char *path, br_entity *out, uint32_t cap,
                        char *err, size_t err_size);

/* A field's value as text in the file syntax, and back. Used by the
 * serializer and handy for tools (the editor's copy/paste, tooltips). */
void br_field_format(const br_field *f, const void *component, char *out, size_t size);
bool br_field_parse(const br_field *f, void *component, const char *text);

#endif /* BRONTO_SERIALIZE_H */
