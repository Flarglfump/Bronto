/*
 * Bronto Engine - Entity Component System.
 *
 * Sparse-set ECS:
 *   - An entity is a 32-bit handle: 20-bit index + 12-bit generation, so
 *     stale handles to destroyed entities are detected. 0 is the null entity.
 *   - Each component type has its own pool: a sparse array (entity index ->
 *     dense slot) and tightly packed dense arrays of entities and data, so
 *     iteration is a linear walk over contiguous memory.
 *   - Components are registered at runtime with a size, so any plain C
 *     struct works and scripting / editor tooling can add types later.
 *   - Queries iterate the smallest matching pool and test the others.
 *     Queries walk backwards, which makes it safe to destroy the current
 *     entity or remove its components while iterating. Adding components to
 *     the pools being iterated may move their data: re-fetch pointers after.
 *   - Systems are plain functions grouped into phases and run in order.
 *   - Components can be *described* (br_component_desc): a list of their
 *     fields with names and types. Described components can be saved to
 *     scene files (serialize.h) and edited in the editor, with no extra
 *     code per component.
 */
#ifndef BRONTO_ECS_H
#define BRONTO_ECS_H

#include "types.h"
#include <stddef.h>   /* offsetof, for BR_FIELD */

typedef uint32_t br_entity;
typedef uint32_t br_component;   /* component type id */
typedef uint32_t br_system;      /* system id         */

#define BR_ENTITY_NULL         0u
#define BR_ENTITY_INDEX_BITS   20u
#define BR_ENTITY_INDEX_MASK   ((1u << BR_ENTITY_INDEX_BITS) - 1u)
#define BR_ENTITY_GEN_MASK     0xFFFu
#define BR_ENTITY_MAX          (1u << BR_ENTITY_INDEX_BITS)
#define BR_ENTITY_INDEX(e)     ((e) & BR_ENTITY_INDEX_MASK)
#define BR_ENTITY_GEN(e)       ((e) >> BR_ENTITY_INDEX_BITS)

#define BR_MAX_COMPONENTS        128
#define BR_QUERY_MAX_COMPONENTS  8
#define BR_QUERY_MAX_EXCLUDE     4
#define BR_COMPONENT_INVALID     0xFFFFFFFFu

/* ---- component descriptions ---------------------------------------------
 * A description lists a component's fields so that generic code (scene
 * files, the editor's inspector) can read and write them:
 *
 *   typedef struct Health { int hp; float regen; bool shield; } Health;
 *   static const br_field k_health_fields[] = {
 *       BR_FIELD(Health, hp, BR_FIELD_INT, 0, 999),
 *       BR_FIELD(Health, regen, BR_FIELD_FLOAT, 0, 10),
 *       BR_FIELD(Health, shield, BR_FIELD_BOOL, 0, 0),
 *   };
 *   static const Health k_health_default = { 100, 0.5f, false };
 *   static const br_component_desc k_health = BR_COMPONENT_DESC(Health, k_health_fields, &k_health_default);
 *   br_component HEALTH = br_component_register_desc(world, &k_health);  */
typedef enum br_field_type {
    BR_FIELD_INT = 0,   /* int                                        */
    BR_FIELD_FLOAT,     /* float                                      */
    BR_FIELD_BOOL,      /* bool                                       */
    BR_FIELD_VEC2,      /* br_vec2                                    */
    BR_FIELD_COLOR,     /* br_color                                   */
    BR_FIELD_STRING,    /* char[N], always NUL terminated              */
    BR_FIELD_ENUM,      /* int, shown and saved by name (enum_names)  */
    BR_FIELD_ENTITY     /* br_entity: a reference to another entity. Scene
                           files save it as that entity's position in the
                           file (or "none"), and loading reconnects it */
} br_field_type;

typedef struct br_field {
    const char *name;
    br_field_type type;
    uint32_t offset, size;            /* where in the component, bytes */
    float min, max;                   /* editor range; min == max = unlimited */
    const char *const *enum_names;    /* BR_FIELD_ENUM: value i is called enum_names[i] */
    int enum_count;
} br_field;

typedef struct br_component_desc {
    const char *name;
    size_t size;
    const br_field *fields;
    int field_count;
    const void *defaults;             /* new components start as a copy (NULL = zeroed) */
} br_component_desc;

#define BR_FIELD(T, member, type, min, max) \
    { #member, type, (uint32_t)offsetof(T, member), (uint32_t)sizeof(((T *)0)->member), min, max, NULL, 0 }
#define BR_FIELD_ENUM_OF(T, member, names) \
    { #member, BR_FIELD_ENUM, (uint32_t)offsetof(T, member), (uint32_t)sizeof(((T *)0)->member), 0, 0, \
      names, (int)(sizeof(names) / sizeof((names)[0])) }
#define BR_COMPONENT_DESC(T, fields, defaults) \
    { #T, sizeof(T), fields, (int)(sizeof(fields) / sizeof((fields)[0])), defaults }

struct br_world;
typedef void (*br_component_hook)(struct br_world *w, br_entity e, void *data, void *user);

typedef struct br_component_pool {
    char *name;
    size_t size;           /* user size (0 = tag)            */
    size_t stride;         /* storage size per element (>=1) */
    uint32_t *sparse;      /* entity index -> dense slot + 1 (0 = absent) */
    br_entity *entities;   /* dense */
    uint8_t *data;         /* dense */
    uint32_t count, capacity;
    br_component_hook on_add, on_remove;
    void *hook_user;
    const br_component_desc *desc;   /* NULL for undescribed (runtime-only) components */
} br_component_pool;

typedef enum br_system_phase {
    BR_PHASE_FIXED_UPDATE = 0, /* fixed timestep (physics, gameplay sim) */
    BR_PHASE_UPDATE,           /* once per frame, variable dt           */
    BR_PHASE_LATE_UPDATE,      /* after update (camera follow...)       */
    BR_PHASE_RENDER,           /* draw                                  */
    BR_PHASE_COUNT
} br_system_phase;

typedef void (*br_system_fn)(struct br_world *w, float dt, void *user);

typedef struct br_system_def {
    char *name;
    br_system_phase phase;
    int order;
    br_system_fn fn;
    void *user;
    bool enabled;
    br_system id;
} br_system_def;

#define BR_WORLD_ATTACHMENTS 8

typedef struct br_world {
    /* entities */
    uint16_t *generations;   /* per index */
    uint8_t *alive;          /* per index */
    uint32_t *free_list;
    uint32_t free_count;
    uint32_t capacity;       /* allocated indices         */
    uint32_t high_water;     /* indices ever handed out   */
    uint32_t alive_count;

    br_component_pool pools[BR_MAX_COMPONENTS];
    uint32_t pool_count;

    br_system_def *systems;
    uint32_t system_count, system_capacity;
    br_system next_system_id;

    br_entity *deferred;
    uint32_t deferred_count, deferred_capacity;

    void *context;  /* owner pointer (the engine sets this to br_engine*) */
    void *user;     /* free for game use (scenes set it to br_scene*)     */

    /* called after an entity is destroyed (the hierarchy destroys its children) */
    struct { void (*fn)(struct br_world *w, br_entity e, void *user); void *user; } destroy_hooks[4];
    int destroy_hook_count;

    /* per-world state of engine modules (physics contacts, script runtime) */
    struct { const void *key; void *data; void (*destroy)(void *data); } attached[BR_WORLD_ATTACHMENTS];
} br_world;

bool br_world_init(br_world *w);
/* Module state that lives as long as the world: `key` is any unique
 * address (a static in the module), `destroy` (may be NULL) runs when the
 * world is destroyed, after its entities. Returns false when full. */
bool  br_world_attach(br_world *w, const void *key, void *data, void (*destroy)(void *data));
void *br_world_attached(const br_world *w, const void *key);
/* `fn` runs after any entity is destroyed (its components are gone, and its
 * handle is no longer alive). Up to 4 per world. */
bool br_world_on_destroy(br_world *w, void (*fn)(br_world *w, br_entity e, void *user), void *user);
void br_world_destroy(br_world *w);
/* Destroys all entities but keeps component types and systems. */
void br_world_clear(br_world *w);

/* ---- entities ---------------------------------------------------------- */
br_entity br_entity_create(br_world *w);
void      br_entity_destroy(br_world *w, br_entity e);
void      br_entity_destroy_deferred(br_world *w, br_entity e); /* at next flush */
void      br_world_flush(br_world *w);
bool      br_entity_alive(const br_world *w, br_entity e);
/* A new entity with a copy of every component of `src` (BR_ENTITY_NULL on failure). */
br_entity br_entity_clone(br_world *w, br_entity src);
uint32_t  br_world_entity_count(const br_world *w);

/* ---- components -------------------------------------------------------- */
br_component br_component_register(br_world *w, const char *name, size_t size);
br_component br_component_find(const br_world *w, const char *name);
/* Registers (or finds) a described component; the description must outlive the world. */
br_component br_component_register_desc(br_world *w, const br_component_desc *desc);
const br_component_desc *br_component_get_desc(const br_world *w, br_component c);   /* or NULL */
const char *br_component_name(const br_world *w, br_component c);
uint32_t br_component_type_count(const br_world *w);
void br_component_set_hooks(br_world *w, br_component c, br_component_hook on_add,
                            br_component_hook on_remove, void *user);

/* Adds (or overwrites) a component. init may be NULL: the description's
 * defaults, or zeroed. Returns the component data, valid until the pool
 * is next modified. */
void *br_add(br_world *w, br_entity e, br_component c, const void *init);
void *br_get(const br_world *w, br_entity e, br_component c);
bool  br_has(const br_world *w, br_entity e, br_component c);
bool  br_remove(br_world *w, br_entity e, br_component c);
uint32_t br_component_count(const br_world *w, br_component c);

/* Convenience macros for typed use:
 *   br_component POS = BR_COMPONENT(w, Position);
 *   Position *p = BR_ADD(w, e, POS, Position, { 1, 2 });
 *   Position *p = BR_GET(w, e, POS, Position);                       */
#define BR_COMPONENT(w, T) br_component_register((w), #T, sizeof(T))
#define BR_ADD(w, e, c, T, ...) ((T *)br_add((w), (e), (c), &(T)__VA_ARGS__))
#define BR_GET(w, e, c, T) ((T *)br_get((w), (e), (c)))

/* ---- queries ------------------------------------------------------------ */
typedef struct br_query {
    br_world *world;
    br_component with[BR_QUERY_MAX_COMPONENTS];
    uint32_t with_count;
    br_component without[BR_QUERY_MAX_EXCLUDE];
    uint32_t without_count;
    uint32_t driver;   /* pool used to drive iteration */
    int64_t cursor;    /* dense index, iterates downward */
    bool started;
    /* results of the current step */
    br_entity entity;
    void *data[BR_QUERY_MAX_COMPONENTS];
} br_query;

br_query br_query_make(br_world *w, const br_component *with, uint32_t count);
void br_query_exclude(br_query *q, br_component c);
bool br_query_next(br_query *q);
uint32_t br_query_count(br_query q);  /* counts matches (runs a fresh pass) */
#define BR_QUERY_GET(q, i, T) ((T *)(q)->data[(i)])

/* ---- systems ------------------------------------------------------------ */
br_system br_system_add(br_world *w, const char *name, br_system_phase phase,
                        br_system_fn fn, void *user, int order);
void br_system_remove(br_world *w, br_system s);
void br_system_set_enabled(br_world *w, br_system s, bool enabled);
/* Runs every enabled system of a phase in ascending order, then flushes
 * deferred destruction. */
void br_world_run_phase(br_world *w, br_system_phase phase, float dt);

#endif /* BRONTO_ECS_H */
