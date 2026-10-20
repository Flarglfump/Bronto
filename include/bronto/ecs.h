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
 */
#ifndef BRONTO_ECS_H
#define BRONTO_ECS_H

#include "types.h"

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
} br_world;

bool br_world_init(br_world *w);
void br_world_destroy(br_world *w);
/* Destroys all entities but keeps component types and systems. */
void br_world_clear(br_world *w);

/* ---- entities ---------------------------------------------------------- */
br_entity br_entity_create(br_world *w);
void      br_entity_destroy(br_world *w, br_entity e);
void      br_entity_destroy_deferred(br_world *w, br_entity e); /* at next flush */
void      br_world_flush(br_world *w);
bool      br_entity_alive(const br_world *w, br_entity e);
uint32_t  br_world_entity_count(const br_world *w);

/* ---- components -------------------------------------------------------- */
br_component br_component_register(br_world *w, const char *name, size_t size);
br_component br_component_find(const br_world *w, const char *name);
void br_component_set_hooks(br_world *w, br_component c, br_component_hook on_add,
                            br_component_hook on_remove, void *user);

/* Adds (or overwrites) a component. init may be NULL (zeroed). Returns the
 * component data, valid until the pool is next modified. */
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
