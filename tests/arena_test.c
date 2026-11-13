/* Copyright 2026 XMOS LIMITED.
 * SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "mpeg2.h"
#include "arena.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

int main(void)
{
    benchmark_arena_install();
    unsigned char *first = mpeg2_malloc(65, MPEG2_ALLOC_CHUNK);
    unsigned char *second = mpeg2_malloc(100, MPEG2_ALLOC_YUV);
    CHECK(first && second && first != second);
    CHECK(!((uintptr_t)first & 63) && !((uintptr_t)second & 63));
    CHECK(benchmark_arena_live() == 165);
    CHECK(benchmark_arena_peak() >= 165);
    CHECK(benchmark_arena_guards_ok());
    /* These writes are within the backing arena but outside the allocation:
     * prove the explicit guards catch what ordinary ASan cannot see here. */
    first[65] = 0;
    CHECK(!benchmark_arena_guards_ok());
    first[65] = 0xa5;
    first[-1] = 0;
    CHECK(!benchmark_arena_guards_ok());
    first[-1] = 0xa5;
    CHECK(benchmark_arena_guards_ok());
    mpeg2_free(first);
    CHECK(benchmark_arena_live() == 100);
    mpeg2_free(second);
    CHECK(!benchmark_arena_live());
    benchmark_arena_install();
    CHECK(!benchmark_arena_peak());
    first = mpeg2_malloc(65, MPEG2_ALLOC_CHUNK);
    CHECK(first && benchmark_arena_guards_ok());
    mpeg2_free(first);
    puts("PASS: arena alignment, guards, live bytes and reset");
    return 0;
}
