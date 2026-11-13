// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <platform.h>
void run_plmpeg_benchmark(void);
int main(void)
{
    par {
        on tile[1]: run_plmpeg_benchmark();
    }
    return 0;
}
