// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdio.h>
#include <stdlib.h>
#include "plmpeg_benchmark.h"
/* Provisional 16 KiB reservation, not an automatically proved stack bound.
 * The push adapter installs no callbacks, but upstream's unused callback
 * branches still prevent complete automatic analysis. */
#pragma stackfunction 4096
void run_plmpeg_benchmark(void)
{
    printf("Scalar PL_MPEG benchmark on tile 1; no VPU or hardware validation\n");
    if (plmpeg_benchmark("VIDEO.M1V", NULL, 4096)) exit(1);
}
