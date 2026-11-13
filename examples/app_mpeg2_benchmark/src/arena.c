/* Copyright 2026 XMOS LIMITED.
 * SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpeg2.h"
#include "arena.h"

enum { MAX_BLOCKS = 16, ALIGNMENT = 64, GUARD_BYTES = 64 };
/* XTC supports declarations aligned to at most eight bytes. Overallocate and
 * align the arena base explicitly, preserving upstream's 64-byte contract. */
static unsigned char storage[MPEG2_ARENA_BYTES + ALIGNMENT - 1] __attribute__((aligned(8)));
static unsigned char *arena;
static struct { void *ptr; size_t size; } blocks[MAX_BLOCKS];
static size_t used, peak, live;

static void *allocate(unsigned size, mpeg2_alloc_t reason)
{
    (void)reason;
    if (!size) return NULL;
    size_t start = (used + GUARD_BYTES + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1);
    unsigned slot;
    for (slot = 0; slot < MAX_BLOCKS && blocks[slot].ptr; ++slot) {}
    if (slot == MAX_BLOCKS || start > MPEG2_ARENA_BYTES ||
        size > MPEG2_ARENA_BYTES - start || GUARD_BYTES > MPEG2_ARENA_BYTES - start - size) {
        fprintf(stderr, "MPEG2 arena exhausted: request=%u used=%lu capacity=%u\n",
                size, (unsigned long)used, MPEG2_ARENA_BYTES);
        abort();
    }
    memset(arena + start - GUARD_BYTES, 0xa5, GUARD_BYTES);
    memset(arena + start + size, 0xa5, GUARD_BYTES);
    blocks[slot].ptr = arena + start;
    blocks[slot].size = size;
    used = start + size + GUARD_BYTES;
    if (used > peak) peak = used;
    live += size;
    return blocks[slot].ptr;
}

int benchmark_arena_guards_ok(void)
{
    for (unsigned i = 0; i < MAX_BLOCKS; ++i) {
        if (!blocks[i].ptr) continue;
        const unsigned char *p = blocks[i].ptr;
        for (unsigned j = 0; j < GUARD_BYTES; ++j)
            if (p[-GUARD_BYTES + (int)j] != 0xa5 || p[blocks[i].size + j] != 0xa5)
                return 0;
    }
    return 1;
}

static int release(void *ptr)
{
    if (!ptr) return 1;
    if (!benchmark_arena_guards_ok()) {
        fprintf(stderr, "MPEG2 allocation guard corrupted\n");
        abort();
    }
    for (unsigned i = 0; i < MAX_BLOCKS; ++i) {
        if (blocks[i].ptr == ptr) {
            live -= blocks[i].size;
            blocks[i].ptr = NULL;
            if (!live) used = 0;
            return 1;
        }
    }
    fprintf(stderr, "MPEG2 attempted to free an unknown allocation\n");
    abort();
}

void benchmark_arena_install(void)
{
    if (live) abort();
    memset(blocks, 0, sizeof(blocks));
    arena = (unsigned char *)(((uintptr_t)storage + ALIGNMENT - 1) & ~(uintptr_t)(ALIGNMENT - 1));
    used = peak = live = 0;
    mpeg2_malloc_hooks(allocate, release);
}

size_t benchmark_arena_peak(void) { return peak; }
size_t benchmark_arena_live(void) { return live; }
