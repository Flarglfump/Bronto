/*
 * Bronto Engine - memory utilities.
 *
 * All engine allocations go through br_alloc/br_realloc/br_free so that
 * allocation counts can be tracked (leak checks in tests) and so the
 * backing allocator can be swapped (e.g. for a custom heap on console/web).
 */
#ifndef BRONTO_MEMORY_H
#define BRONTO_MEMORY_H

#include "types.h"

/* ---- Pluggable heap allocator ---------------------------------------- */
typedef struct br_allocator {
    void *(*alloc)(void *user, size_t size);
    void *(*realloc)(void *user, void *ptr, size_t old_size, size_t new_size);
    void  (*free)(void *user, void *ptr, size_t size);
    void *user;
} br_allocator;

typedef struct br_mem_stats {
    size_t live_allocations; /* allocations not yet freed            */
    size_t live_bytes;       /* bytes in live allocations            */
    size_t total_allocations;/* allocations ever made                */
    size_t peak_bytes;       /* high-water mark of live_bytes        */
} br_mem_stats;

/* Replace the global allocator (NULL restores the stdlib default). Do this
 * before any engine allocation happens. */
void br_set_allocator(const br_allocator *a);

void *br_alloc(size_t size);                 /* uninitialised */
void *br_calloc(size_t count, size_t size);  /* zeroed        */
void *br_realloc(void *ptr, size_t new_size);
void  br_free(void *ptr);
char *br_strdup(const char *s);

br_mem_stats br_mem_get_stats(void);

#define BR_NEW(T)        ((T *)br_calloc(1, sizeof(T)))
#define BR_NEW_ARRAY(T,n) ((T *)br_calloc((n), sizeof(T)))

/* ---- Linear arena allocator ------------------------------------------
 * Fast bump allocation; everything is released at once with reset().
 * Ideal for per-frame scratch memory. */
typedef struct br_arena {
    uint8_t *base;
    size_t   capacity;
    size_t   used;
    size_t   peak;
    bool     owns_memory;
} br_arena;

bool  br_arena_init(br_arena *a, size_t capacity);
void  br_arena_init_buffer(br_arena *a, void *buffer, size_t capacity);
void  br_arena_destroy(br_arena *a);
void *br_arena_alloc(br_arena *a, size_t size);          /* zeroed, 16-byte aligned */
void *br_arena_alloc_aligned(br_arena *a, size_t size, size_t align);
void  br_arena_reset(br_arena *a);
size_t br_arena_mark(const br_arena *a);
void  br_arena_restore(br_arena *a, size_t mark);

/* ---- Fixed-size object pool ------------------------------------------
 * O(1) alloc/free of equally sized blocks from a single slab. */
typedef struct br_pool {
    uint8_t *memory;
    size_t   block_size;
    size_t   block_count;
    size_t   used_count;
    void    *free_list;
} br_pool;

bool  br_pool_init(br_pool *p, size_t block_size, size_t block_count);
void  br_pool_destroy(br_pool *p);
void *br_pool_alloc(br_pool *p);    /* zeroed; NULL when exhausted */
void  br_pool_free(br_pool *p, void *block);

/* ---- File helpers (stdio based, works on every target) --------------- */
/* Reads an entire file. Returns br_alloc'd buffer (NUL terminated, size
 * excludes the terminator) or NULL. Free with br_free. */
void *br_file_read_all(const char *path, size_t *out_size);
bool  br_file_write_all(const char *path, const void *data, size_t size);

#endif /* BRONTO_MEMORY_H */
