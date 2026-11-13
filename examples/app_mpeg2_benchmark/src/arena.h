/* Copyright 2026 XMOS LIMITED.
 * SPDX-License-Identifier: MIT */
#ifndef MPEG2_BENCH_ARENA_H_
#define MPEG2_BENCH_ARENA_H_
#include <stddef.h>
enum { MPEG2_ARENA_BYTES = 400 * 1024 };
/* Single decoder/task only. OOM is fatal in this evaluation harness because
 * upstream does not consistently check its internal allocation results. */
void benchmark_arena_install(void);
size_t benchmark_arena_peak(void);
size_t benchmark_arena_live(void);
int benchmark_arena_guards_ok(void);
#endif
