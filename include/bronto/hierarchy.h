/*
 * Bronto Engine - the scene tree.
 *
 * Entities can be arranged in a tree, like Godot's nodes or Unity's
 * GameObjects: an entity with a Parent component is a child of that
 * entity. Its Transform is then relative to the parent (local), so moving,
 * turning or scaling a parent carries its whole subtree along, and
 * destroying a parent destroys its children too.
 *
 *   Parent    { entity }   the parent; no Parent component = a root
 *
 * Everything that places things in the world (drawing, picking, physics)
 * uses the world transform, br_world_transform(). Motion and scripts'
 * `position` work in the parent's space; scripts' `global_position` in
 * the world's.
 *
 * Children are kept in the order of the Parent pool (the order they were
 * parented). The editor shows and saves them in its own tree order.
 */
#ifndef BRONTO_HIERARCHY_H
#define BRONTO_HIERARCHY_H

#include "ecs.h"
#include "components.h"

typedef struct br_parent { br_entity entity; } br_parent;

/* Registers Parent and the hook that destroys children with their parent
 * (br_builtin_register does this). */
void br_hierarchy_register(br_world *w);
const br_component_desc *br_hierarchy_desc(void);

br_entity br_parent_of(const br_world *w, br_entity e);            /* BR_ENTITY_NULL for a root */
bool      br_is_ancestor(const br_world *w, br_entity ancestor, br_entity e);
uint32_t  br_depth(const br_world *w, br_entity e);                /* 0 for a root */

/* Makes `child` a child of `parent` (BR_ENTITY_NULL: a root). With
 * keep_world, its local Transform is adjusted so it stays where it is on
 * screen. False if that would make a loop (parenting to its own
 * descendant) or either entity is dead. */
bool br_set_parent(br_world *w, br_entity child, br_entity parent, bool keep_world);

/* Direct children, in order; returns how many there are (fills up to `cap`). */
uint32_t br_children(const br_world *w, br_entity e, br_entity *out, uint32_t cap);
/* The direct child called `name`, or BR_ENTITY_NULL. */
br_entity br_child_named(const br_world *w, br_entity e, const char *name);

/* ---- transforms --------------------------------------------------------------
 * world = parent's world  then  local: position = parent position +
 * rotate(parent rotation, parent scale * local position), rotation adds,
 * scale multiplies. An entity without a Transform passes its parent's
 * through (it is a plain grouping node). */
br_transform br_world_transform(const br_world *w, br_entity e);
br_transform br_compose(const br_transform *parent, const br_transform *local);
/* The local transform that puts `e` at `world`, given its current parent. */
br_transform br_local_from_world(const br_world *w, br_entity e, const br_transform *world);
void br_set_world_position(br_world *w, br_entity e, br_vec2 position);
void br_set_world_rotation(br_world *w, br_entity e, float degrees);
/* A world-space direction or offset as the parent's space sees it (for
 * nudging a child by screen pixels). */
br_vec2 br_world_to_parent_vector(const br_world *w, br_entity e, br_vec2 v);

/* Destroys `e` and every descendant (br_entity_destroy does the same). */
void br_destroy_tree(br_world *w, br_entity e);

/* `e` and all its descendants, depth first (parents before children);
 * returns the count (fills up to `cap`). */
uint32_t br_subtree(const br_world *w, br_entity e, br_entity *out, uint32_t cap);

#endif /* BRONTO_HIERARCHY_H */
