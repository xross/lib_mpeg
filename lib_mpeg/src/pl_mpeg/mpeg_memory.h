// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#ifndef LIB_MPEG_MEMORY_H_
#define LIB_MPEG_MEMORY_H_
#include <stddef.h>
#include <stdint.h>
#include "mpeg.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Bounded allocator over memory the application passes to an open call. One
 * arena per decoder. PL_MPEG's allocation macros carry no context, so each
 * adapter entry point selects its arena for the calling thread first. */
enum { MPEG_MEMORY_BLOCKS = 8 };
typedef struct {
    uint8_t *base;
    size_t capacity, used, peak, live;
    struct { uint8_t *ptr; size_t size; } allocations[MPEG_MEMORY_BLOCKS];
} mpeg_arena_t;

/* Returns zero if memory is null, misaligned or an arena is still live. */
int mpeg_memory_init(mpeg_arena_t *arena, void *memory, size_t size);
void mpeg_memory_select(mpeg_arena_t *arena);
/* Upper bound on the arena bytes one allocation of this size consumes. */
#define MPEG_MEMORY_BLOCK(size) ((size) + 2 * 16 + 7)
/* Bytes still available to allocations, by the same measure. */
size_t mpeg_memory_room(const mpeg_arena_t *arena);
mpeg_memory_stats_t mpeg_memory_arena_stats(const mpeg_arena_t *arena);

void *mpeg_memory_alloc(size_t size);
void *mpeg_memory_resize(void *ptr, size_t size);
void mpeg_memory_free(void *ptr);
#ifdef __cplusplus
}
#endif
#endif
