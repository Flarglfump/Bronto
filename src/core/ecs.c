#include "bronto/ecs.h"
#include "bronto/memory.h"
#include "bronto/log.h"

#include <string.h>

#define INITIAL_ENTITY_CAPACITY 256u

static bool grow_entities(br_world *w, uint32_t min_capacity) {
    uint32_t cap = w->capacity ? w->capacity : INITIAL_ENTITY_CAPACITY, i;
    uint16_t *gens;
    uint8_t *alive;
    uint32_t *fl;
    while (cap < min_capacity) cap *= 2;
    if (cap > BR_ENTITY_MAX) cap = BR_ENTITY_MAX;
    if (cap <= w->capacity) return false;

    gens = (uint16_t *)br_realloc(w->generations, cap * sizeof(uint16_t));
    if (!gens) return false;
    w->generations = gens;
    alive = (uint8_t *)br_realloc(w->alive, cap);
    if (!alive) return false;
    w->alive = alive;
    fl = (uint32_t *)br_realloc(w->free_list, cap * sizeof(uint32_t));
    if (!fl) return false;
    w->free_list = fl;
    for (i = w->capacity; i < cap; ++i) { w->generations[i] = 1; w->alive[i] = 0; }

    for (i = 0; i < w->pool_count; ++i) {
        br_component_pool *p = &w->pools[i];
        uint32_t *sp = (uint32_t *)br_realloc(p->sparse, cap * sizeof(uint32_t));
        if (!sp) return false;
        memset(sp + w->capacity, 0, (cap - w->capacity) * sizeof(uint32_t));
        p->sparse = sp;
    }
    w->capacity = cap;
    return true;
}

bool br_world_init(br_world *w) {
    memset(w, 0, sizeof(*w));
    w->next_system_id = 1;
    return grow_entities(w, INITIAL_ENTITY_CAPACITY);
}

bool br_world_attach(br_world *w, const void *key, void *data, void (*destroy)(void *data)) {
    int i;
    for (i = 0; i < BR_WORLD_ATTACHMENTS; ++i)
        if (!w->attached[i].key || w->attached[i].key == key) {
            w->attached[i].key = key;
            w->attached[i].data = data;
            w->attached[i].destroy = destroy;
            return true;
        }
    return false;
}

void *br_world_attached(const br_world *w, const void *key) {
    int i;
    for (i = 0; i < BR_WORLD_ATTACHMENTS; ++i) if (w->attached[i].key == key) return w->attached[i].data;
    return NULL;
}

void br_world_destroy(br_world *w) {
    uint32_t i;
    br_world_clear(w);
    for (i = 0; i < BR_WORLD_ATTACHMENTS; ++i)
        if (w->attached[i].key && w->attached[i].destroy) w->attached[i].destroy(w->attached[i].data);
    for (i = 0; i < w->pool_count; ++i) {
        br_component_pool *p = &w->pools[i];
        br_free(p->name);
        br_free(p->sparse);
        br_free(p->entities);
        br_free(p->data);
    }
    for (i = 0; i < w->system_count; ++i) br_free(w->systems[i].name);
    br_free(w->systems);
    br_free(w->generations);
    br_free(w->alive);
    br_free(w->free_list);
    br_free(w->deferred);
    memset(w, 0, sizeof(*w));
}

void br_world_clear(br_world *w) {
    uint32_t i;
    for (i = 0; i < w->high_water; ++i) {
        if (w->alive[i]) {
            br_entity e = (br_entity)(((uint32_t)w->generations[i] << BR_ENTITY_INDEX_BITS) | i);
            br_entity_destroy(w, e);
        }
    }
    w->deferred_count = 0;
}

/* ---- entities ---------------------------------------------------------- */

br_entity br_entity_create(br_world *w) {
    uint32_t idx;
    if (w->free_count > 0) {
        idx = w->free_list[--w->free_count];
    } else {
        if (w->high_water >= w->capacity && !grow_entities(w, w->capacity * 2)) {
            BR_ERROR("entity limit reached (%u)", w->capacity);
            return BR_ENTITY_NULL;
        }
        idx = w->high_water++;
    }
    w->alive[idx] = 1;
    w->alive_count++;
    return (br_entity)(((uint32_t)w->generations[idx] << BR_ENTITY_INDEX_BITS) | idx);
}

br_entity br_entity_clone(br_world *w, br_entity src) {
    br_entity e;
    uint32_t c;
    if (!br_entity_alive(w, src)) return BR_ENTITY_NULL;
    e = br_entity_create(w);
    if (e == BR_ENTITY_NULL) return e;
    for (c = 0; c < w->pool_count; ++c) {
        const void *data = br_get(w, src, c);
        unsigned char copy[1024];
        if (!data) continue;
        if (w->pools[c].size > sizeof(copy)) {          /* big component: copy via the heap */
            void *tmp = br_alloc(w->pools[c].size);
            if (!tmp) continue;
            memcpy(tmp, data, w->pools[c].size);
            br_add(w, e, c, tmp);
            br_free(tmp);
        } else {
            memcpy(copy, data, w->pools[c].size);       /* br_add may move the source */
            br_add(w, e, c, w->pools[c].size ? copy : NULL);
        }
    }
    return e;
}

bool br_entity_alive(const br_world *w, br_entity e) {
    uint32_t idx = BR_ENTITY_INDEX(e);
    if (e == BR_ENTITY_NULL || idx >= w->high_water) return false;
    return w->alive[idx] && w->generations[idx] == BR_ENTITY_GEN(e);
}

void br_entity_destroy(br_world *w, br_entity e) {
    uint32_t idx = BR_ENTITY_INDEX(e), i;
    uint16_t gen;
    if (!br_entity_alive(w, e)) return;
    for (i = 0; i < w->pool_count; ++i) br_remove(w, e, i);
    w->alive[idx] = 0;
    gen = (uint16_t)((w->generations[idx] + 1) & BR_ENTITY_GEN_MASK);
    w->generations[idx] = gen ? gen : 1;   /* generation 0 is reserved */
    w->free_list[w->free_count++] = idx;
    w->alive_count--;
    for (i = 0; i < (uint32_t)w->destroy_hook_count; ++i) w->destroy_hooks[i].fn(w, e, w->destroy_hooks[i].user);
}

bool br_world_on_destroy(br_world *w, void (*fn)(br_world *w, br_entity e, void *user), void *user) {
    int i;
    for (i = 0; i < w->destroy_hook_count; ++i) if (w->destroy_hooks[i].fn == fn) return true;
    if (w->destroy_hook_count == 4) return false;
    w->destroy_hooks[w->destroy_hook_count].fn = fn;
    w->destroy_hooks[w->destroy_hook_count++].user = user;
    return true;
}

void br_entity_destroy_deferred(br_world *w, br_entity e) {
    if (!br_entity_alive(w, e)) return;
    if (w->deferred_count == w->deferred_capacity) {
        uint32_t cap = w->deferred_capacity ? w->deferred_capacity * 2 : 64;
        br_entity *n = (br_entity *)br_realloc(w->deferred, cap * sizeof(br_entity));
        if (!n) return;
        w->deferred = n;
        w->deferred_capacity = cap;
    }
    w->deferred[w->deferred_count++] = e;
}

void br_world_flush(br_world *w) {
    uint32_t i;
    /* Destroy hooks may defer more destruction; loop until stable. */
    for (i = 0; i < w->deferred_count; ++i) br_entity_destroy(w, w->deferred[i]);
    w->deferred_count = 0;
}

uint32_t br_world_entity_count(const br_world *w) { return w->alive_count; }

/* ---- components -------------------------------------------------------- */

br_component br_component_find(const br_world *w, const char *name) {
    uint32_t i;
    for (i = 0; i < w->pool_count; ++i)
        if (strcmp(w->pools[i].name, name) == 0) return i;
    return BR_COMPONENT_INVALID;
}

br_component br_component_register(br_world *w, const char *name, size_t size) {
    br_component existing = br_component_find(w, name);
    br_component_pool *p;
    if (existing != BR_COMPONENT_INVALID) {
        if (w->pools[existing].size != size)
            BR_WARN("component '%s' re-registered with a different size", name);
        return existing;
    }
    if (w->pool_count >= BR_MAX_COMPONENTS) {
        BR_ERROR("too many component types (max %d)", BR_MAX_COMPONENTS);
        return BR_COMPONENT_INVALID;
    }
    p = &w->pools[w->pool_count];
    memset(p, 0, sizeof(*p));
    p->name = br_strdup(name);
    p->size = size;
    p->stride = size ? (size + 7u) & ~(size_t)7u : 1;
    p->sparse = (uint32_t *)br_calloc(w->capacity, sizeof(uint32_t));
    if (!p->sparse) return BR_COMPONENT_INVALID;
    return w->pool_count++;
}

br_component br_component_register_desc(br_world *w, const br_component_desc *desc) {
    br_component c = br_component_register(w, desc->name, desc->size);
    if (c != BR_COMPONENT_INVALID) w->pools[c].desc = desc;
    return c;
}

const br_component_desc *br_component_get_desc(const br_world *w, br_component c) {
    return c < w->pool_count ? w->pools[c].desc : NULL;
}

const char *br_component_name(const br_world *w, br_component c) {
    return c < w->pool_count ? w->pools[c].name : NULL;
}

uint32_t br_component_type_count(const br_world *w) { return w->pool_count; }

void br_component_set_hooks(br_world *w, br_component c, br_component_hook on_add,
                            br_component_hook on_remove, void *user) {
    if (c >= w->pool_count) return;
    w->pools[c].on_add = on_add;
    w->pools[c].on_remove = on_remove;
    w->pools[c].hook_user = user;
}

static bool pool_reserve(br_component_pool *p, uint32_t needed) {
    uint32_t cap;
    br_entity *ents;
    uint8_t *data;
    if (needed <= p->capacity) return true;
    cap = p->capacity ? p->capacity * 2 : 64;
    while (cap < needed) cap *= 2;
    ents = (br_entity *)br_realloc(p->entities, cap * sizeof(br_entity));
    if (!ents) return false;
    p->entities = ents;
    data = (uint8_t *)br_realloc(p->data, cap * p->stride);
    if (!data) return false;
    p->data = data;
    p->capacity = cap;
    return true;
}

void *br_add(br_world *w, br_entity e, br_component c, const void *init) {
    br_component_pool *p;
    uint32_t idx = BR_ENTITY_INDEX(e), slot;
    void *dst;
    if (c >= w->pool_count || !br_entity_alive(w, e)) return NULL;
    p = &w->pools[c];
    if (p->sparse[idx]) {               /* already present: overwrite */
        dst = p->data + (size_t)(p->sparse[idx] - 1) * p->stride;
        if (!init && p->desc) init = p->desc->defaults;
        if (p->size) { if (init) memcpy(dst, init, p->size); else memset(dst, 0, p->size); }
        return dst;
    }
    if (!pool_reserve(p, p->count + 1)) return NULL;
    slot = p->count++;
    p->entities[slot] = e;
    p->sparse[idx] = slot + 1;
    dst = p->data + (size_t)slot * p->stride;
    memset(dst, 0, p->stride);
    if (!init && p->desc) init = p->desc->defaults;
    if (init && p->size) memcpy(dst, init, p->size);
    if (p->on_add) p->on_add(w, e, dst, p->hook_user);
    /* The hook might add to this same pool; re-resolve the pointer. */
    return p->data + (size_t)(p->sparse[idx] - 1) * p->stride;
}

void *br_get(const br_world *w, br_entity e, br_component c) {
    const br_component_pool *p;
    uint32_t idx = BR_ENTITY_INDEX(e), s;
    if (c >= w->pool_count || !br_entity_alive(w, e)) return NULL;
    p = &w->pools[c];
    s = p->sparse[idx];
    return s ? p->data + (size_t)(s - 1) * p->stride : NULL;
}

bool br_has(const br_world *w, br_entity e, br_component c) {
    return c < w->pool_count && br_entity_alive(w, e) && w->pools[c].sparse[BR_ENTITY_INDEX(e)] != 0;
}

bool br_remove(br_world *w, br_entity e, br_component c) {
    br_component_pool *p;
    uint32_t idx = BR_ENTITY_INDEX(e), slot, last;
    if (c >= w->pool_count || !br_entity_alive(w, e)) return false;
    p = &w->pools[c];
    if (!p->sparse[idx]) return false;
    if (p->on_remove) {
        p->on_remove(w, e, p->data + (size_t)(p->sparse[idx] - 1) * p->stride, p->hook_user);
        if (!p->sparse[idx]) return true;  /* hook removed it itself */
    }
    slot = p->sparse[idx] - 1;
    last = p->count - 1;
    if (slot != last) {  /* swap-remove keeps the dense arrays packed */
        br_entity moved = p->entities[last];
        p->entities[slot] = moved;
        memcpy(p->data + (size_t)slot * p->stride, p->data + (size_t)last * p->stride, p->stride);
        p->sparse[BR_ENTITY_INDEX(moved)] = slot + 1;
    }
    p->sparse[idx] = 0;
    p->count--;
    return true;
}

uint32_t br_component_count(const br_world *w, br_component c) {
    return c < w->pool_count ? w->pools[c].count : 0;
}

/* ---- queries ------------------------------------------------------------ */

br_query br_query_make(br_world *w, const br_component *with, uint32_t count) {
    br_query q;
    uint32_t i;
    memset(&q, 0, sizeof(q));
    q.world = w;
    if (count > BR_QUERY_MAX_COMPONENTS) {
        BR_ERROR("query has too many components (%u > %d)", count, BR_QUERY_MAX_COMPONENTS);
        count = BR_QUERY_MAX_COMPONENTS;
    }
    for (i = 0; i < count; ++i) q.with[i] = with[i];
    q.with_count = count;
    return q;
}

void br_query_exclude(br_query *q, br_component c) {
    if (q->without_count < BR_QUERY_MAX_EXCLUDE) q->without[q->without_count++] = c;
}

bool br_query_next(br_query *q) {
    br_world *w = q->world;
    uint32_t i;
    if (!q->started) {
        uint32_t best = BR_COMPONENT_INVALID, best_count = 0xFFFFFFFFu;
        q->started = true;
        if (q->with_count == 0) { q->cursor = -1; return false; }
        for (i = 0; i < q->with_count; ++i) {
            if (q->with[i] >= w->pool_count) { q->cursor = -1; return false; }
            if (w->pools[q->with[i]].count < best_count) {
                best_count = w->pools[q->with[i]].count;
                best = q->with[i];
            }
        }
        q->driver = best;
        q->cursor = (int64_t)best_count;
    }
    for (;;) {
        br_component_pool *drv = &w->pools[q->driver];
        br_entity e;
        uint32_t idx;
        bool match = true;
        q->cursor--;
        /* The driver pool may have shrunk (entities removed during
         * iteration); clamp so we never read past the end. */
        if (q->cursor >= (int64_t)drv->count) q->cursor = (int64_t)drv->count - 1;
        if (q->cursor < 0) return false;
        e = drv->entities[q->cursor];
        idx = BR_ENTITY_INDEX(e);
        for (i = 0; i < q->with_count && match; ++i) {
            br_component_pool *p = &w->pools[q->with[i]];
            if (!p->sparse[idx]) match = false;
            else q->data[i] = p->data + (size_t)(p->sparse[idx] - 1) * p->stride;
        }
        for (i = 0; i < q->without_count && match; ++i) {
            br_component c = q->without[i];
            if (c < w->pool_count && w->pools[c].sparse[idx]) match = false;
        }
        if (match) { q->entity = e; return true; }
    }
}

uint32_t br_query_count(br_query q) {
    uint32_t n = 0;
    q.started = false;
    while (br_query_next(&q)) ++n;
    return n;
}

/* ---- systems ------------------------------------------------------------ */

static void sort_systems(br_world *w) {
    uint32_t i, j;
    for (i = 1; i < w->system_count; ++i) {
        br_system_def tmp = w->systems[i];
        j = i;
        while (j > 0 && (w->systems[j - 1].phase > tmp.phase ||
                         (w->systems[j - 1].phase == tmp.phase && w->systems[j - 1].order > tmp.order))) {
            w->systems[j] = w->systems[j - 1];
            --j;
        }
        w->systems[j] = tmp;
    }
}

br_system br_system_add(br_world *w, const char *name, br_system_phase phase,
                        br_system_fn fn, void *user, int order) {
    br_system_def *s;
    if (!fn || (int)phase < 0 || phase >= BR_PHASE_COUNT) return 0;
    if (w->system_count == w->system_capacity) {
        uint32_t cap = w->system_capacity ? w->system_capacity * 2 : 16;
        br_system_def *n = (br_system_def *)br_realloc(w->systems, cap * sizeof(br_system_def));
        if (!n) return 0;
        w->systems = n;
        w->system_capacity = cap;
    }
    s = &w->systems[w->system_count++];
    s->name = br_strdup(name ? name : "system");
    s->phase = phase;
    s->order = order;
    s->fn = fn;
    s->user = user;
    s->enabled = true;
    s->id = w->next_system_id++;
    sort_systems(w);
    return w->next_system_id - 1;
}

void br_system_remove(br_world *w, br_system id) {
    uint32_t i;
    for (i = 0; i < w->system_count; ++i) {
        if (w->systems[i].id == id) {
            br_free(w->systems[i].name);
            memmove(&w->systems[i], &w->systems[i + 1],
                    (w->system_count - i - 1) * sizeof(br_system_def));
            w->system_count--;
            return;
        }
    }
}

void br_system_set_enabled(br_world *w, br_system id, bool enabled) {
    uint32_t i;
    for (i = 0; i < w->system_count; ++i)
        if (w->systems[i].id == id) w->systems[i].enabled = enabled;
}

void br_world_run_phase(br_world *w, br_system_phase phase, float dt) {
    uint32_t i;
    for (i = 0; i < w->system_count; ++i) {
        br_system_def s = w->systems[i];
        if (s.phase != phase || !s.enabled) continue;
        s.fn(w, dt, s.user);
    }
    br_world_flush(w);
}
