/* Copyright 2026 XMOS LIMITED.
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include "mpeg2_benchmark.h"
int main(int argc, char **argv)
{
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "Usage: %s input.m2v [output.yuv|-] [read_bytes]\n", argv[0]);
        return 1;
    }
    unsigned bytes = 4096;
    if (argc == 4) {
        char *end;
        unsigned long value = strtoul(argv[3], &end, 10);
        if (*end || !value || value > 4096) return 1;
        bytes = (unsigned)value;
    }
    const char *output = argc >= 3 && !(argv[2][0] == '-' && !argv[2][1]) ? argv[2] : NULL;
    return mpeg2_benchmark(argv[1], output, bytes);
}
