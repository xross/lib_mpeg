// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include "mpeg_rgb565.h"

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error The packed RGB565 tables assume a little-endian target
#endif

/* Match PL_MPEG's limited-range BT.601 scalar conversion:
 *   y = ((Y - 16) * 76309) >> 16,       range  -18..278
 *   r = (v * 104597) >> 16,             range -205..202
 *   g = (u * 25674 + v * 53278) >> 16,  range -155..153
 *   b = (u * 132201) >> 16,             range -259..256
 * with red = clamp(y + r), green = clamp(y - g), blue = clamp(y + b).
 * Products fit int32; signed right shifts are arithmetic on XTC and test hosts.
 *
 * Luma scaling and each channel's clamp, RGB565 field placement and byte order
 * are tabulated, so a pixel costs one luma lookup, three channel lookups and
 * two ORs. Channel sums span -277..534; BIAS makes them indices. */
enum { BIAS = 288, CHANNEL_ENTRIES = 832 };
static int16_t luma[256];
static uint16_t red565[CHANNEL_ENTRIES], green565[CHANNEL_ENTRIES], blue565[CHANNEL_ENTRIES];
static int tables_ready;

typedef uint32_t __attribute__((may_alias)) pixel32_t;

/* The in-memory (little-endian) 16 bit value whose bytes are `pixel`, high first. */
static uint16_t high_byte_first(unsigned pixel)
{
    return (uint16_t)((pixel >> 8) | ((pixel & 255) << 8));
}

static void build_tables(void)
{
    for (int i = 0; i < 256; ++i) luma[i] = (int16_t)(((i - 16) * 76309) >> 16);
    for (int i = 0; i < CHANNEL_ENTRIES; ++i) {
        int value = i - BIAS;
        unsigned level = value < 0 ? 0 : value > 255 ? 255 : (unsigned)value;
        red565[i] = high_byte_first((level & 0xf8) << 8);
        green565[i] = high_byte_first((level & 0xfc) << 3);
        blue565[i] = high_byte_first(level >> 3);
    }
    tables_ready = 1;
}

void mpeg_yuv420_rows_to_rgb565(const uint8_t *y0, const uint8_t *y1,
                                const uint8_t *cb, const uint8_t *cr,
                                uint8_t *out0, uint8_t *out1, unsigned width)
{
    if (!tables_ready) build_tables();
    /* Two pixels form one 32-bit store when both output rows are word aligned
     * (unaligned word access traps on xcore); otherwise store bytes. */
    int aligned = !(((uintptr_t)out0 | (uintptr_t)out1) & 3);
    for (unsigned x = 0; x < width; x += 2) {
        int u = (int)cb[x / 2] - 128, v = (int)cr[x / 2] - 128;
        const uint16_t *red = red565 + BIAS + ((v * 104597) >> 16);
        const uint16_t *green = green565 + BIAS - ((u * 25674 + v * 53278) >> 16);
        const uint16_t *blue = blue565 + BIAS + ((u * 132201) >> 16);
        int a = luma[y0[x]], b = luma[y0[x + 1]], c = luma[y1[x]], d = luma[y1[x + 1]];
        unsigned top = (unsigned)(red[a] | green[a] | blue[a]) |
                       ((unsigned)(red[b] | green[b] | blue[b]) << 16);
        unsigned bottom = (unsigned)(red[c] | green[c] | blue[c]) |
                          ((unsigned)(red[d] | green[d] | blue[d]) << 16);
        if (aligned) {
            *(pixel32_t *)(out0 + 2 * x) = top;
            *(pixel32_t *)(out1 + 2 * x) = bottom;
        } else {
            out0[2 * x] = (uint8_t)top; out0[2 * x + 1] = (uint8_t)(top >> 8);
            out0[2 * x + 2] = (uint8_t)(top >> 16); out0[2 * x + 3] = (uint8_t)(top >> 24);
            out1[2 * x] = (uint8_t)bottom; out1[2 * x + 1] = (uint8_t)(bottom >> 8);
            out1[2 * x + 2] = (uint8_t)(bottom >> 16); out1[2 * x + 3] = (uint8_t)(bottom >> 24);
        }
    }
}

void mpeg_frame_to_rgb565(const mpeg_frame_t *frame, uint8_t *destination, unsigned stride)
{
    for (unsigned y = 0; y < frame->height; y += 2) {
        /* The last row of an odd-height frame has no partner: convert it with
         * itself, writing the same output row twice. */
        unsigned lower = y + 1 < frame->height ? y + 1 : y;
        mpeg_yuv420_rows_to_rgb565(
            frame->plane[0] + y * frame->stride[0], frame->plane[0] + lower * frame->stride[0],
            frame->plane[1] + (y / 2) * frame->stride[1], frame->plane[2] + (y / 2) * frame->stride[2],
            destination + y * stride, destination + lower * stride, frame->width);
    }
}
