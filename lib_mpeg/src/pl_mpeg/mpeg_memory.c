// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdlib.h>
#include <string.h>
#include "mpeg.h"
#include "mpeg_memory.h"

enum { BLOCKS = 8, ALIGN = 8, GUARD = 16 };
static uint8_t arena[MPEG_ARENA_CAPACITY] __attribute__((aligned(8)));
static struct { uint8_t *ptr; size_t size; } allocations[BLOCKS];
static size_t used, peak, live;

mpeg_memory_stats_t mpeg_memory_stats(void)
{
    mpeg_memory_stats_t stats = {peak, live, sizeof(arena), 1};
    for (unsigned i = 0; i < BLOCKS; ++i) {
        if (!allocations[i].ptr) continue;
        const uint8_t *p = allocations[i].ptr;
        for (unsigned j = 0; j < GUARD; ++j)
            if (p[(int)j - GUARD] != 0xa5 || p[allocations[i].size + j] != 0xa5)
                stats.guards_ok = 0;
    }
    return stats;
}

void mpeg_memory_reset(void)
{
    if (live) abort();
    memset(allocations, 0, sizeof(allocations));
    used = peak = live = 0;
}

void *mpeg_memory_alloc(size_t size)
{
    if (!size) return NULL;
    size_t begin = (used + GUARD + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    unsigned i;
    for (i = 0; i < BLOCKS && allocations[i].ptr; ++i) {}
    /* Upstream dereferences allocation results without checking. The bounded
     * adapter checks dimensions/feed sizes before entering it. Unexpected
     * backend allocation requests fail fatally, never fall back to a heap. */
    if (i == BLOCKS || begin > sizeof(arena) || size > sizeof(arena) - begin ||
        GUARD > sizeof(arena) - begin - size) abort();
    allocations[i].ptr = arena + begin;
    allocations[i].size = size;
    memset(arena + begin - GUARD, 0xa5, GUARD);
    memset(arena + begin + size, 0xa5, GUARD);
    used = begin + size + GUARD;
    if (used > peak) peak = used;
    live += size;
    return arena + begin;
}

void *mpeg_memory_resize(void *ptr, size_t size)
{
    if (!ptr) return mpeg_memory_alloc(size);
    if (!size) { mpeg_memory_free(ptr); return NULL; }
    for (unsigned i = 0; i < BLOCKS; ++i)
        if (allocations[i].ptr == ptr && size <= allocations[i].size) return ptr;
    /* The supported adapter never grows its compressed buffer. */
    abort();
}

void mpeg_memory_free(void *ptr)
{
    if (!ptr) return;
    if (!mpeg_memory_stats().guards_ok) abort();
    for (unsigned i = 0; i < BLOCKS; ++i) {
        if (allocations[i].ptr == ptr) {
            live -= allocations[i].size;
            allocations[i].ptr = NULL;
            if (!live) used = 0;
            return;
        }
    }
    abort();
}
