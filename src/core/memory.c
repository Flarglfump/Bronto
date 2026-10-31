#include "bronto/memory.h"
#include "bronto/log.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every heap block carries a small header recording its size so that the
 * stats are exact and custom allocators receive the size on free. The
 * header is 16 bytes to preserve max_align_t alignment on all targets. */
typedef struct br_alloc_header {
    size_t size;
    size_t magic;
} br_alloc_header;

#define BR_ALLOC_MAGIC ((size_t)0xB4027011u)
#define BR_HEADER_SIZE 16u
BR_STATIC_ASSERT(sizeof(br_alloc_header) <= BR_HEADER_SIZE, header_fits);

static void *default_alloc(void *u, size_t s) { BR_UNUSED(u); return malloc(s); }
static void *default_realloc(void *u, void *p, size_t o, size_t n) {
    BR_UNUSED(u); BR_UNUSED(o); return realloc(p, n);
}
static void default_free(void *u, void *p, size_t s) { BR_UNUSED(u); BR_UNUSED(s); free(p); }

static br_allocator g_alloc = { default_alloc, default_realloc, default_free, NULL };
static br_mem_stats g_stats;

void br_assert_fail(const char *expr, const char *file, int line) {
    br_log_write(BR_LOG_FATAL, file, line, "Assertion failed: %s", expr);
    abort();
}

void br_set_allocator(const br_allocator *a) {
    if (a) g_alloc = *a;
    else {
        g_alloc.alloc = default_alloc;
        g_alloc.realloc = default_realloc;
        g_alloc.free = default_free;
        g_alloc.user = NULL;
    }
}

static void stats_add(size_t bytes) {
    g_stats.live_allocations++;
    g_stats.total_allocations++;
    g_stats.live_bytes += bytes;
    if (g_stats.live_bytes > g_stats.peak_bytes) g_stats.peak_bytes = g_stats.live_bytes;
}

static void stats_remove(size_t bytes) {
    g_stats.live_allocations--;
    g_stats.live_bytes -= bytes;
}

void *br_alloc(size_t size) {
    uint8_t *raw;
    br_alloc_header *h;
    if (size == 0) size = 1;
    raw = (uint8_t *)g_alloc.alloc(g_alloc.user, size + BR_HEADER_SIZE);
    if (!raw) { BR_ERROR("br_alloc: out of memory (%llu bytes)", (unsigned long long)size); return NULL; }
    h = (br_alloc_header *)raw;
    h->size = size;
    h->magic = BR_ALLOC_MAGIC;
    stats_add(size);
    return raw + BR_HEADER_SIZE;
}

void *br_calloc(size_t count, size_t size) {
    size_t total;
    void *p;
    if (size != 0 && count > ((size_t)-1 - BR_HEADER_SIZE) / size) return NULL;
    total = count * size;
    p = br_alloc(total);
    if (p) memset(p, 0, total ? total : 1);
    return p;
}

static br_alloc_header *header_of(void *ptr) {
    br_alloc_header *h = (br_alloc_header *)((uint8_t *)ptr - BR_HEADER_SIZE);
    BR_ASSERT(h->magic == BR_ALLOC_MAGIC && "pointer not allocated by br_alloc");
    return h;
}

void *br_realloc(void *ptr, size_t new_size) {
    br_alloc_header *h;
    size_t old_size;
    uint8_t *raw;
    if (!ptr) return br_alloc(new_size);
    if (new_size == 0) new_size = 1;
    h = header_of(ptr);
    old_size = h->size;
    raw = (uint8_t *)g_alloc.realloc(g_alloc.user, h, old_size + BR_HEADER_SIZE,
                                     new_size + BR_HEADER_SIZE);
    if (!raw) { BR_ERROR("br_realloc: out of memory (%llu bytes)", (unsigned long long)new_size); return NULL; }
    h = (br_alloc_header *)raw;
    h->size = new_size;
    g_stats.live_bytes = g_stats.live_bytes - old_size + new_size;
    if (g_stats.live_bytes > g_stats.peak_bytes) g_stats.peak_bytes = g_stats.live_bytes;
    return raw + BR_HEADER_SIZE;
}

void br_free(void *ptr) {
    br_alloc_header *h;
    size_t size;
    if (!ptr) return;
    h = header_of(ptr);
    size = h->size;
    h->magic = 0;
    stats_remove(size);
    g_alloc.free(g_alloc.user, h, size + BR_HEADER_SIZE);
}

char *br_strdup(const char *s) {
    size_t n;
    char *d;
    if (!s) return NULL;
    n = strlen(s) + 1;
    d = (char *)br_alloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

br_mem_stats br_mem_get_stats(void) { return g_stats; }

/* ---- Arena ------------------------------------------------------------ */

bool br_arena_init(br_arena *a, size_t capacity) {
    memset(a, 0, sizeof(*a));
    a->base = (uint8_t *)br_alloc(capacity);
    if (!a->base) return false;
    a->capacity = capacity;
    a->owns_memory = true;
    return true;
}

void br_arena_init_buffer(br_arena *a, void *buffer, size_t capacity) {
    memset(a, 0, sizeof(*a));
    a->base = (uint8_t *)buffer;
    a->capacity = capacity;
}

void br_arena_destroy(br_arena *a) {
    if (a->owns_memory) br_free(a->base);
    memset(a, 0, sizeof(*a));
}

void *br_arena_alloc_aligned(br_arena *a, size_t size, size_t align) {
    uintptr_t cur, aligned;
    size_t offset;
    BR_ASSERT(align && (align & (align - 1)) == 0);
    cur = (uintptr_t)(a->base + a->used);
    aligned = (cur + (align - 1)) & ~(uintptr_t)(align - 1);
    offset = (size_t)(aligned - (uintptr_t)a->base);
    if (offset > a->capacity || size > a->capacity - offset) return NULL;
    a->used = offset + size;
    if (a->used > a->peak) a->peak = a->used;
    memset(a->base + offset, 0, size);
    return a->base + offset;
}

void *br_arena_alloc(br_arena *a, size_t size) { return br_arena_alloc_aligned(a, size, 16); }
void br_arena_reset(br_arena *a) { a->used = 0; }
size_t br_arena_mark(const br_arena *a) { return a->used; }
void br_arena_restore(br_arena *a, size_t mark) { if (mark <= a->used) a->used = mark; }

/* ---- Pool ------------------------------------------------------------- */

bool br_pool_init(br_pool *p, size_t block_size, size_t block_count) {
    size_t i;
    memset(p, 0, sizeof(*p));
    if (block_size < sizeof(void *)) block_size = sizeof(void *);
    block_size = (block_size + 15u) & ~(size_t)15u;
    p->memory = (uint8_t *)br_alloc(block_size * block_count);
    if (!p->memory) return false;
    p->block_size = block_size;
    p->block_count = block_count;
    /* Thread the free list through the blocks, lowest address first. */
    for (i = block_count; i-- > 0;) {
        void **blk = (void **)(p->memory + i * block_size);
        *blk = p->free_list;
        p->free_list = blk;
    }
    return true;
}

void br_pool_destroy(br_pool *p) { br_free(p->memory); memset(p, 0, sizeof(*p)); }

void *br_pool_alloc(br_pool *p) {
    void **blk = (void **)p->free_list;
    if (!blk) return NULL;
    p->free_list = *blk;
    p->used_count++;
    memset(blk, 0, p->block_size);
    return blk;
}

void br_pool_free(br_pool *p, void *block) {
    if (!block) return;
    BR_ASSERT((uint8_t *)block >= p->memory &&
              (uint8_t *)block < p->memory + p->block_size * p->block_count);
    *(void **)block = p->free_list;
    p->free_list = block;
    p->used_count--;
}

/* ---- Files ------------------------------------------------------------ */

static br_file_reader g_reader;

void br_file_set_reader(br_file_reader reader) { g_reader = reader; }

void *br_file_read_all(const char *path, size_t *out_size) {
    FILE *f;
    long len;
    char *buf;
    if (out_size) *out_size = 0;
    if (g_reader) {
        void *found = g_reader(path, out_size);
        if (found) return found;
    }
    f = fopen(path, "rb");
    if (!f) return NULL;
    /* (a directory opens on some systems, and then reports a size of LONG_MAX) */
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || len == LONG_MAX || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    if (len == 0) {                                      /* (a folder can open, empty: reading it fails) */
        char c;
        if (fread(&c, 1, 1, f) == 0 && ferror(f)) { fclose(f); return NULL; }
        rewind(f);
    }
    buf = (char *)br_alloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        br_free(buf);
        fclose(f);
        return NULL;
    }
    buf[len] = '\0';
    fclose(f);
    if (out_size) *out_size = (size_t)len;
    return buf;
}

bool br_file_write_all(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    bool ok;
    if (!f) return false;
    ok = fwrite(data, 1, size, f) == size;
    ok = (fclose(f) == 0) && ok;
    return ok;
}
