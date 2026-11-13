/* Copyright 2026 XMOS LIMITED.
 * SPDX-License-Identifier: MIT */
#include <platform.h>
void run_mpeg2_benchmark(void);
int main(void)
{
    par {
        on tile[1]: run_mpeg2_benchmark();
    }
    return 0;
}
