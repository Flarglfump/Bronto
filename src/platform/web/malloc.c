/*
 * Bronto web runtime - malloc, free, realloc, calloc.
 *
 * A boundary-tag allocator over WebAssembly's linear memory (grown with
 * memory.grow when it runs out). Each block has a one-word header: its
 * size, "in use", and "the block before is in use". A free block also
 * records its size in its last word, so neighbours can be merged when
 * blocks are freed, and sits in one of 96 lists by size: one list per
 * 16 bytes up to 1 KB, then one per power of two (split in half), so a fit
 * is found quickly and memory isn't scattered into unusable pieces.
 * Blocks are 16-byte aligned. Single-threaded, as the web build is.
 */
#include "bw.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define ALIGN      16u
#define HDR        sizeof(size_t)                 /* 4 bytes on wasm32 */
#define MIN_BLOCK  16u                            /* header + two list links + footer fit in 16 */
#define USED       1u
#define PREV_USED  2u
#define FLAGS      3u
#define SMALL_BINS 64u                            /* 16, 32, ... 1024 */
#define BINS       96u
#define PAGE       65536u

typedef struct blk { size_t head; struct blk *next, *prev; } blk;   /* a free block's first words */

extern unsigned char __heap_base;
static unsigned char *g_start, *g_top, *g_end;   /* blocks run from g_start up to the top block at g_top */
static blk *g_bins[BINS];
static uint32_t g_nonempty[BINS / 32];

#define SIZE(b)        ((b)->head & ~(size_t)FLAGS)
#define NEXT_BLOCK(b)  ((blk *)((unsigned char *)(b) + SIZE(b)))
#define PAYLOAD(b)     ((void *)((unsigned char *)(b) + HDR))
#define BLOCK_OF(p)    ((blk *)((unsigned char *)(p) - HDR))

static unsigned bin_of(size_t size) {
    unsigned log = 0;
    size_t s;
    if (size <= SMALL_BINS * 16) return (unsigned)(size / 16) - 1;
    for (s = size >> 10; s > 1; s >>= 1) ++log;                       /* 1 KB..2 KB -> 0 */
    {
        unsigned b = SMALL_BINS + log * 2 + (unsigned)((size >> (9 + log)) & 1);   /* half-steps */
        return b < BINS ? b : BINS - 1;
    }
}

static void set_footer(blk *b) { *(size_t *)((unsigned char *)b + SIZE(b) - HDR) = SIZE(b); }

static void bin_insert(blk *b) {
    unsigned i = bin_of(SIZE(b));
    b->prev = NULL;
    b->next = g_bins[i];
    if (b->next) b->next->prev = b;
    g_bins[i] = b;
    g_nonempty[i / 32] |= 1u << (i % 32);
}
static void bin_remove(blk *b) {
    unsigned i = bin_of(SIZE(b));
    if (b->prev) b->prev->next = b->next;
    else g_bins[i] = b->next;
    if (b->next) b->next->prev = b->prev;
    if (!g_bins[i]) g_nonempty[i / 32] &= ~(1u << (i % 32));
}

static bool heap_init(void) {
    uintptr_t start = ((uintptr_t)&__heap_base + HDR + ALIGN - 1) / ALIGN * ALIGN - HDR;   /* payloads 16-aligned */
    uintptr_t end = (uintptr_t)__builtin_wasm_memory_size(0) * PAGE;
    if (end < start + 4 * ALIGN) {
        if (__builtin_wasm_memory_grow(0, 16) == (size_t)-1) return false;
        end = (uintptr_t)__builtin_wasm_memory_size(0) * PAGE;
    }
    g_start = g_top = (unsigned char *)start;
    g_end = (unsigned char *)end;
    ((blk *)g_top)->head = PREV_USED;               /* the top block: everything left, size implied */
    return true;
}

/* Makes the top block at least `need` bytes (it always keeps its header). */
static bool grow_top(size_t need) {
    size_t have = (size_t)(g_end - g_top), pages;
    if (have >= need + HDR) return true;
    pages = (need + HDR - have + PAGE - 1) / PAGE;
    if (pages < 16) pages = 16;                     /* grow 1 MB at a time at least */
    if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) {
        pages = (need + HDR - have + PAGE - 1) / PAGE;
        if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) return false;
    }
    g_end = (unsigned char *)((uintptr_t)__builtin_wasm_memory_size(0) * PAGE);
    return true;
}

static size_t block_size_for(size_t n) {
    size_t s;
    if (n > (size_t)-1 / 2) return 0;
    s = (n + HDR + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    return s < MIN_BLOCK ? MIN_BLOCK : s;
}

/* Marks b (free, out of the bins) used with `size` bytes, freeing any rest. */
static void use_block(blk *b, size_t size) {
    size_t total = SIZE(b), flags = b->head & PREV_USED;
    if (total - size >= MIN_BLOCK) {
        blk *rest = (blk *)((unsigned char *)b + size);
        b->head = size | flags | USED;
        rest->head = (total - size) | PREV_USED;
        set_footer(rest);
        bin_insert(rest);                           /* (its next block's PREV_USED is already clear) */
    } else {
        b->head = total | flags | USED;
        if ((unsigned char *)NEXT_BLOCK(b) == g_top) ((blk *)g_top)->head |= PREV_USED;
        else NEXT_BLOCK(b)->head |= PREV_USED;
    }
}

void *malloc(size_t n) {
    size_t size = block_size_for(n);
    unsigned i;
    blk *b;
    if (!size) { errno = ENOMEM; return NULL; }
    if (!g_start && !heap_init()) { errno = ENOMEM; return NULL; }
    /* 1. the first list that can hold it; the exact-size lists need no checking */
    for (i = bin_of(size); i < BINS; ++i) {
        uint32_t bits = g_nonempty[i / 32] >> (i % 32);
        if (!bits) { i = (i / 32 + 1) * 32 - 1; continue; }
        i += (unsigned)__builtin_ctz(bits);
        if (i >= BINS) break;
        for (b = g_bins[i]; b; b = b->next) {
            if (SIZE(b) >= size) {
                bin_remove(b);
                use_block(b, size);
                return PAYLOAD(b);
            }
        }
    }
    /* 2. carve it from the top */
    if (!grow_top(size)) { errno = ENOMEM; return NULL; }
    b = (blk *)g_top;
    b->head = size | (b->head & PREV_USED) | USED;
    g_top += size;
    ((blk *)g_top)->head = PREV_USED;
    return PAYLOAD(b);
}

void free(void *p) {
    blk *b, *next;
    size_t size;
    if (!p) return;
    b = BLOCK_OF(p);
    size = SIZE(b);
    if (!(b->head & PREV_USED)) {                   /* merge with the free block before */
        size_t before = *(size_t *)((unsigned char *)b - HDR);
        blk *prev = (blk *)((unsigned char *)b - before);
        bin_remove(prev);
        size += before;
        b = prev;
        b->head = size | (b->head & PREV_USED);
    } else {
        b->head = size | PREV_USED;
    }
    next = (blk *)((unsigned char *)b + size);
    if ((unsigned char *)next == g_top) {           /* give it back to the top */
        g_top = (unsigned char *)b;
        ((blk *)g_top)->head = b->head & PREV_USED;
        return;
    }
    if (!(next->head & USED)) {                     /* merge with the free block after */
        bin_remove(next);
        size += SIZE(next);
        b->head = size | (b->head & PREV_USED);
        next = (blk *)((unsigned char *)b + size);
        if ((unsigned char *)next == g_top) {
            g_top = (unsigned char *)b;
            ((blk *)g_top)->head = b->head & PREV_USED;
            return;
        }
    }
    next->head &= ~(size_t)PREV_USED;
    set_footer(b);
    bin_insert(b);
}

void *realloc(void *p, size_t n) {
    blk *b, *next;
    size_t size, have;
    void *q;
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    if (!(size = block_size_for(n))) { errno = ENOMEM; return NULL; }
    b = BLOCK_OF(p);
    have = SIZE(b);
    if (size <= have) {                             /* shrink in place */
        if (have - size >= MIN_BLOCK) {
            blk *rest = (blk *)((unsigned char *)b + size);
            b->head = size | (b->head & PREV_USED) | USED;
            rest->head = (have - size) | PREV_USED | USED;
            free(PAYLOAD(rest));
        }
        return p;
    }
    next = NEXT_BLOCK(b);
    if ((unsigned char *)next == g_top) {           /* the last block: just take more of the top */
        if (grow_top(size - have)) {
            b->head = size | (b->head & PREV_USED) | USED;
            g_top = (unsigned char *)b + size;
            ((blk *)g_top)->head = PREV_USED;
            return p;
        }
    } else if (!(next->head & USED) && have + SIZE(next) >= size) {   /* absorb the free block after */
        size_t total = have + SIZE(next);
        bin_remove(next);
        b->head = total | (b->head & PREV_USED);   /* temporarily "free-sized" for use_block */
        use_block(b, size);
        return p;
    }
    if (!(q = malloc(n))) return NULL;
    memcpy(q, p, have - HDR);
    free(p);
    return q;
}

void *calloc(size_t count, size_t size) {
    void *p;
    if (size && count > (size_t)-1 / size) { errno = ENOMEM; return NULL; }
    p = malloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}

void *aligned_alloc(size_t align, size_t size) {
    if (align <= ALIGN) return malloc(size);
    errno = EINVAL;                                 /* nothing in Bronto needs more */
    return NULL;
}
