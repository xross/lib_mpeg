// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include "mpeg_rgb565_vpu.h"

/* Host model of the assembly's 16-bit coefficient decomposition and rounded
 * VLSAT. The exhaustive independent upstream test checks this arithmetic;
 * the same test on XS3 checks the actual instructions, ZIP and lane layout. */
static int floor_q16(int product)
{
    int accumulator = product - 32768;
    return ((accumulator >> 15) + 1) >> 1;
}

static unsigned quantize(int value, unsigned bits)
{
    int level = value >> (8 - bits);
    int maximum = (1 << bits) - 1;
    return level < 0 ? 0 : level > maximum ? (unsigned)maximum : (unsigned)level;
}

void mpeg_rgb565_vpu_block(const mpeg_rgb565_vpu_input_t *input,
                           uint8_t *out0, uint8_t *out1)
{
    const int8_t *u = (const int8_t *)input->cb;
    const int8_t *v = (const int8_t *)input->cr;
    for (unsigned row = 0; row < 2; ++row) {
        const int8_t *samples = (const int8_t *)(row ? input->y1 : input->y0);
        uint8_t *out = row ? out1 : out0;
        for (unsigned i = 0; i < 16; ++i) {
            int t = samples[i] + 112;
            int y = t + floor_q16(t * 10773);
            int r = 2 * v[i] + floor_q16(v[i] * -26475);
            int g = v[i] + floor_q16(u[i] * 25674 - v[i] * 12258);
            int b = 2 * u[i] + floor_q16(u[i] * 1129);
            unsigned red = quantize(y + r, 5), green = quantize(y - g, 6);
            unsigned blue = quantize(y + b, 5);
            out[2 * i] = (uint8_t)(red * 8 + (green >> 3));
            out[2 * i + 1] = (uint8_t)((green & 7) * 32 + blue);
        }
    }
}
