// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdlib.h>
#include <string.h>
#include "mpeg_memory.h"

enum { ALIGN = 8, GUARD = 16 };

/* One selection per logical core, so a video and an audio decoder driven from
 * different tasks on one tile cannot allocate from each other's arena. */
#ifdef __xcore__
enum { THREADS = 8 };
static unsigned thread(void)
{
    unsigned id;
    __asm__("get r11, id\n\tmov %0, r11" : "=r"(id) : : "r11");
    return id & (THREADS - 1);
}
#else
enum { THREADS = 1 };
static unsigned thread(void) { return 0; }
#endif
static mpeg_arena_t *selected[THREADS];

int mpeg_memory_init(mpeg_arena_t *arena, void *memory, size_t size)
{
    if (arena->live || !memory || ((uintptr_t)memory & (ALIGN - 1))) return 0;
    memset(arena, 0, sizeof(*arena));
    arena->base = memory;
    arena->capacity = size;
    return 1;
}

void mpeg_memory_select(mpeg_arena_t *arena) { selected[thread()] = arena; }

typedef char block_bound[MPEG_MEMORY_BLOCK(0) == 2 * GUARD + ALIGN - 1 ? 1 : -1];

size_t mpeg_memory_room(const mpeg_arena_t *arena) { return arena->capacity - arena->used; }

mpeg_memory_stats_t mpeg_memory_arena_stats(const mpeg_arena_t *arena)
{
    mpeg_memory_stats_t stats = {arena->peak, arena->live, arena->capacity, 1};
    for (unsigned i = 0; i < MPEG_MEMORY_BLOCKS; ++i) {
        if (!arena->allocations[i].ptr) continue;
        const uint8_t *p = arena->allocations[i].ptr;
        for (unsigned j = 0; j < GUARD; ++j)
            if (p[(int)j - GUARD] != 0xa5 || p[arena->allocations[i].size + j] != 0xa5)
                stats.guards_ok = 0;
    }
    return stats;
}

void *mpeg_memory_alloc(size_t size)
{
    mpeg_arena_t *arena = selected[thread()];
    if (!size) return NULL;
    if (!arena) abort();
    size_t begin = (arena->used + GUARD + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    unsigned i;
    for (i = 0; i < MPEG_MEMORY_BLOCKS && arena->allocations[i].ptr; ++i) {}
    /* Upstream dereferences allocation results without checking. The adapters
     * check their memory, dimensions and feed sizes before entering it.
     * Unexpected backend allocation requests fail fatally, never use a heap. */
    if (i == MPEG_MEMORY_BLOCKS || begin > arena->capacity || size > arena->capacity - begin ||
        GUARD > arena->capacity - begin - size) abort();
    arena->allocations[i].ptr = arena->base + begin;
    arena->allocations[i].size = size;
    memset(arena->base + begin - GUARD, 0xa5, GUARD);
    memset(arena->base + begin + size, 0xa5, GUARD);
    arena->used = begin + size + GUARD;
    if (arena->used > arena->peak) arena->peak = arena->used;
    arena->live += size;
    return arena->base + begin;
}

void *mpeg_memory_resize(void *ptr, size_t size)
{
    if (!ptr) return mpeg_memory_alloc(size);
    if (!size) { mpeg_memory_free(ptr); return NULL; }
    mpeg_arena_t *arena = selected[thread()];
    if (!arena) abort();
    for (unsigned i = 0; i < MPEG_MEMORY_BLOCKS; ++i)
        if (arena->allocations[i].ptr == ptr && size <= arena->allocations[i].size) return ptr;
    /* The supported adapters never grow their compressed buffers. */
    abort();
}

void mpeg_memory_free(void *ptr)
{
    if (!ptr) return;
    mpeg_arena_t *arena = selected[thread()];
    if (!arena || !mpeg_memory_arena_stats(arena).guards_ok) abort();
    for (unsigned i = 0; i < MPEG_MEMORY_BLOCKS; ++i) {
        if (arena->allocations[i].ptr == ptr) {
            arena->live -= arena->allocations[i].size;
            arena->allocations[i].ptr = NULL;
            if (!arena->live) arena->used = 0;
            return;
        }
    }
    abort();
}
