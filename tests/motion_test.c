// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

/* Test the actual fork's kernel, including its aligned-load path on xcore.
 * Compare every output byte with an independent per-pixel MPEG-1 expression. */
static plm_video_t video;
static uint8_t source[64 * 64 + 8] __attribute__((aligned(4)));
static uint8_t actual[sizeof(source)] __attribute__((aligned(4)));
static uint8_t expected[sizeof(source)] __attribute__((aligned(4)));
static uint32_t random_state = 1;

static uint8_t random_byte(void) {
    random_state = random_state * UINT32_C(1664525) + UINT32_C(1013904223);
    return random_state >> 24;
}

int main(void) {
    unsigned cases = 0;
    video.mb_width = video.mb_height = 4;
    video.mb_col = video.mb_row = 1;
    for (int size = 8; size <= 16; size += 8) {
        int stride = video.mb_width * size;
        for (int pattern = 0; pattern < 4; ++pattern) {
            for (unsigned i = 0; i < sizeof(source); ++i) {
                source[i] = pattern == 0 ? 0 : pattern == 1 ? 255 :
                    pattern == 2 ? (i & 1 ? 255 : 0) : random_byte();
            }
            for (int h = -9; h <= 9; ++h) {
                for (int v = -3; v <= 3; ++v) {
                    for (int blend = 0; blend <= 1; ++blend) {
                        for (unsigned i = 0; i < sizeof(actual); ++i)
                            actual[i] = expected[i] = random_byte();
                        int si = (size + (v >> 1)) * stride + size + (h >> 1);
                        int di = size * stride + size;
                        for (int y = 0; y < size; ++y) {
                            for (int x = 0; x < size; ++x) {
                                int p = si + y * stride + x;
                                unsigned value = source[p];
                                if ((h & 1) && (v & 1))
                                    value = (value + source[p + 1] + source[p + stride] +
                                             source[p + stride + 1] + 2) >> 2;
                                else if (h & 1) value = (value + source[p + 1] + 1) >> 1;
                                else if (v & 1) value = (value + source[p + stride] + 1) >> 1;
                                int q = di + y * stride + x;
                                expected[q] = blend ? (expected[q] + value + 1) >> 1 : value;
                            }
                        }
                        plm_video_process_macroblock(&video, source, actual, h, v, size, blend);
                        if (memcmp(actual, expected, sizeof(actual))) {
                            printf("FAIL: motion size=%d pattern=%d h=%d v=%d blend=%d\n",
                                   size, pattern, h, v, blend);
                            return 1;
                        }
                        ++cases;
                    }
                }
            }
        }
    }
    printf("PASS: motion compensation %u cases; 8/16 pixels, all alignments, half-pixels and blending\n", cases);
    return 0;
}
