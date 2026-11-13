/* Copyright 2026 XMOS LIMITED.
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include "mpeg2_benchmark.h"
/* Evaluation-only reservation: the unannotated upstream callback graph prevents
 * automatic stack sizing. Replace this with complete fptrgroup annotations
 * before exposing a production API. Units are 32-bit words (16 KiB here). */
#pragma stackfunction 4096
void run_mpeg2_benchmark(void)
{
    printf("Scalar MPEG-2 benchmark on tile 1; file I/O is outside decode timing\n");
    if (mpeg2_benchmark("VIDEO.M2V", NULL, 4096)) exit(1);
}
