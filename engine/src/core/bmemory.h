#pragma once

#include "defines.h"

typedef enum memory_tag {
    MEMORY_TAG_UNKNOWN, // Catch-all allocation type. Will cause warning.
    
    MEMORY_TAG_ARRAY,
    MEMORY_TAG_DARRAY,
    MEMORY_TAG_DICT,
    MEMORY_TAG_CIRCLE_QUEUE,
    MEMORY_TAG_BST,
    MEMORY_TAG_STRING,
    MEMORY_TAG_APPLICATION,
    MEMORY_TAG_JOB,
    MEMORY_TAG_TEXTURE,
    MEMORY_TAG_MATERIAL_INSTANCE,
    MEMORY_TAG_RENDERER,
    MEMORY_TAG_GAME,
    MEMORY_TAG_TRANSFORM,
    MEMORY_TAG_ENTITY,
    MEMORY_TAG_ENTITY_NODE,
    MEMORY_TAG_SCENE,

    MEMORY_TAG_MAX_TAGS
} memory_tag;

B_API void initialize_memory(); // TODO: remove export later
B_API void shutdown_memory(); // TODO: remove export later

B_API void * b_alloc(u64 size, memory_tag tag);

B_API void b_free(void * block, u64 size, memory_tag tag);

B_API void * b_zero_mem(void * block, u64 size);

B_API void * b_copy_mem(void * dst, const void * src, u64 size);

B_API void * b_set_mem(void * dst, i32 val, u64 size);

B_API char * get_memory_usage_str();
