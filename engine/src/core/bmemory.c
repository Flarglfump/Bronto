#include "bmemory.h"

#include "core/logger.h"
#include "platform/platform.h"

/* TODO: custom string lib */
#include <string.h>
#include <stdio.h>

struct memory_stats {
    u64 total_allocated;
    u64 tagged_allocations[MEMORY_TAG_MAX_TAGS];
};

static const char * memory_tag_strings[MEMORY_TAG_MAX_TAGS] = {
   "UNKNOWN    ",
   "ARRAY      ",
   "DARRAY     ",
   "DICT       ",
   "CIRC_QUEUE ",
   "BST        ",
   "STRING     ",
   "APPLICATION",
   "JOB        ",
   "TEXTURE    ",
   "MAT_INST   ",
   "RENDERER   ",
   "GAME       ",
   "TRANSFORM  ",
   "ENTITY     ",
   "ENTITY_NODE",
   "SCENE      ",
};

/* Private variable to store Bronto memory allocation info */
static struct memory_stats stats;

void initialize_memory() {
    platform_zero_memory(&stats, sizeof(stats));
}
void shutdown_memory() {
    /* Code here to clean up allocations */
}

B_API void * b_alloc(u64 size, memory_tag tag) {
    if (tag == MEMORY_TAG_UNKNOWN) {
        B_WARN("b_alloc() called using unknown tag. Re-class this allocation!");
    }

    stats.total_allocated += size;
    stats.tagged_allocations[tag] += size;

    /* TODO: memory alignment */
    void * block = platform_allocate(size, false);
    platform_zero_memory(block, size);

    return block;
}

B_API void b_free(void * block, u64 size, memory_tag tag) {
    if (tag == MEMORY_TAG_UNKNOWN) {
        B_WARN("b_free() called using unknown tag. Re-class this allocation!");
    }

    stats.total_allocated -= size;
    stats.tagged_allocations[tag] -= size;

    /* TODO: memory alignment */
    platform_free(block, false);
}

B_API void * b_zero_mem(void * block, u64 size) {
    return platform_zero_memory(block, size);
}

B_API void * b_copy_mem(void * dst, const void * src, u64 size) {
    return platform_copy_memory(dst, src, size);
}

B_API void * b_set_mem(void * dst, i32 val, u64 size) {
    return platform_set_memory(dst, val, size);
}

/* Note - returned char array should be freed! */
B_API char * get_memory_usage_str() {
    const u64 gib = 1024 * 1024 * 1024;
    const u64 mib = 1024 * 1024;
    const u64 kib = 1024;

    const int bufsize = 8000;
    char buffer[bufsize] = "System memory use (tagged):\n";
    u64 offset = strlen(buffer);
    for (u32 i = 0; i < MEMORY_TAG_MAX_TAGS; i++) {
        char unit[4] = "xiB";
        float amount = 1.0f;

        if (stats.tagged_allocations[i] >= gib) {
            unit[0] = 'G';
            amount = stats.tagged_allocations[i] / (float) gib;
        } else if (stats.tagged_allocations[i] >= mib) {
            unit[0] = 'M';
            amount = stats.tagged_allocations[i] / (float) mib;
        } else if (stats.tagged_allocations[i] >= kib) {
            unit[0] = 'K';
            amount = stats.tagged_allocations[i] / (float) kib;
        } else {
            unit[0] = 'B';
            unit[1] = 0;
            amount = (float) stats.tagged_allocations[i];
        }

        i32 length = snprintf(buffer + offset, bufsize, "  %s: %.2f%s\n", memory_tag_strings[i], amount, unit);
        offset += length;
    }
    char * out_string = _strdup(buffer);
    return out_string;
}