// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef MPEG_TEST_BIG_ENDIAN
#include "motion_word_model.h"
#endif
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

/* Test the actual fork's kernel, including its aligned-load path on xcore.
 * Compare every output byte with an independent per-pixel MPEG-1 expression. */
static plm_video_t video;
static uint8_t source[64 * 64 + 32] __attribute__((aligned(4)));
static uint8_t actual[sizeof(source)] __attribute__((aligned(4)));
static uint8_t expected[sizeof(source)] __attribute__((aligned(4)));
static uint32_t random_state = 1;

static uint8_t random_byte(void) {
    random_state = random_state * UINT32_C(1664525) + UINT32_C(1013904223);
    return random_state >> 24;
}

static int test_interior(void) {
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

static uint8_t edge_actual[64 * 64 + 64] __attribute__((aligned(4)));
static uint8_t edge_expected[sizeof(edge_actual)] __attribute__((aligned(4)));

static int test_edges(void) {
    unsigned cases = 0;
#ifdef __xcore__
    /* xcore requires aligned frames and padded-word lookahead; the existing
     * XS3 copy hook may read 24 bytes past an eight-byte source row. */
    const unsigned alignments = 1;
#else
    const unsigned alignments = 4;
#endif
    video.mb_width = video.mb_height = 4;
    for (int size = 8; size <= 16; size += 8) {
        int stride = 4 * size;
        unsigned plane_bytes = (unsigned)(stride * stride);
        for (unsigned source_alignment = 0; source_alignment < alignments; ++source_alignment) {
#ifdef __xcore__
            uint8_t *pixels = source;
#else
            /* No source-tail padding on the portable path: ASan catches even
             * a single excess byte at the end of a plane, at every alignment. */
            uint8_t *allocation = malloc(plane_bytes + source_alignment);
            if (!allocation) return 1;
            uint8_t *pixels = allocation + source_alignment;
#endif
            for (int pattern = 0; pattern < 4; ++pattern) {
                for (unsigned i = 0; i < plane_bytes; ++i)
                    pixels[i] = pattern == 0 ? 0 : pattern == 1 ? 255 :
                        pattern == 2 ? (i & 1 ? 255 : 0) : random_byte();
                for (unsigned destination_alignment = 0; destination_alignment < alignments; ++destination_alignment) {
                    uint8_t *output = edge_actual + 32 + destination_alignment;
                    uint8_t *reference = edge_expected + 32 + destination_alignment;
                    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) {
                        video.mb_row = row;
                        video.mb_col = col;
                        for (int h = -9; h <= 9; ++h) for (int v = -3; v <= 3; ++v) {
                            int sx = col * size + (h >> 1), sy = row * size + (v >> 1);
                            if (sx < 0 || sy < 0 || sx + size + (h & 1) > stride || sy + size + (v & 1) > stride)
                                continue;
                            for (int blend = 0; blend <= 1; ++blend) {
                                memset(edge_actual, 0xa5, sizeof(edge_actual));
                                memset(output, 0x5a, plane_bytes);
                                memcpy(edge_expected, edge_actual, sizeof(edge_actual));
                                for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
                                    int p = (sy + y) * stride + sx + x;
                                    unsigned value = pixels[p];
                                    if ((h & 1) && (v & 1)) value = (value + pixels[p + 1] +
                                        pixels[p + stride] + pixels[p + stride + 1] + 2) >> 2;
                                    else if (h & 1) value = (value + pixels[p + 1] + 1) >> 1;
                                    else if (v & 1) value = (value + pixels[p + stride] + 1) >> 1;
                                    int q = (row * size + y) * stride + col * size + x;
                                    reference[q] = blend ? (reference[q] + value + 1) >> 1 : value;
                                }
                                plm_video_process_macroblock(&video, pixels, output, h, v, size, blend);
                                if (memcmp(edge_actual, edge_expected, sizeof(edge_actual))) {
                                    printf("FAIL: edge size=%d row=%d col=%d h=%d v=%d blend=%d source-align=%u destination-align=%u\n",
                                           size, row, col, h, v, blend, source_alignment, destination_alignment);
                                    return 1;
                                }
                                ++cases;
                            }
                        }
                    }
                }
            }
            memset(edge_actual, 0xa5, sizeof(edge_actual));
            memcpy(edge_expected, edge_actual, sizeof(edge_actual));
            video.mb_row = video.mb_col = 0;
            plm_video_process_macroblock(&video, pixels, edge_actual + 32, -2, 0, size, 0);
            video.mb_row = 4;
            plm_video_process_macroblock(&video, pixels, edge_actual + 32, 0, 0, size, 0);
            if (memcmp(edge_actual, edge_expected, sizeof(edge_actual))) return 1;
#ifndef __xcore__
            free(allocation);
#endif
        }
    }
    printf("PASS: motion edges %u cases; plane boundaries, source/destination alignment and address guards\n", cases);
    return 0;
}

int main(void) {
    return test_interior() || test_edges();
}
