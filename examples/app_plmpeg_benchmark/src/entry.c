#include <stdio.h>
#include <stdlib.h>
#include "plmpeg_benchmark.h"
/* Provisional 16 KiB reservation, not an automatically proved stack bound.
 * The push adapter installs no callbacks, but upstream's unused callback
 * branches still prevent complete automatic analysis. */
#pragma stackfunction 4096
void run_plmpeg_benchmark(void)
{
    printf("PL_MPEG decoder benchmark on tile 1; input/output excluded from timing\n");
    if (plmpeg_benchmark("VIDEO.M1V", NULL, 4096)) exit(1);
}
