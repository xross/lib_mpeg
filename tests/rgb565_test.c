// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpeg_rgb565.h"
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#ifdef __xcore__
#include <xcore/hwtimer.h>
#endif

/* Independent colour-conversion oracle: unmodified upstream PL_MPEG produces
 * RGB888; only the truncation to RGB565 is shared with the expected output. */
#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)

static void expect(const uint8_t *rgb888, const uint8_t *rgb565, unsigned pixels)
{
    for (unsigned i = 0; i < pixels; ++i) {
        unsigned pixel = ((rgb888[3 * i] & 0xf8) << 8) | ((rgb888[3 * i + 1] & 0xfc) << 3) |
                         (rgb888[3 * i + 2] >> 3);
        if (rgb565[2 * i] != pixel >> 8 || rgb565[2 * i + 1] != (pixel & 255)) {
            printf("Pixel %u: expected=%04x actual=%02x%02x\n", i, pixel,
                   rgb565[2 * i], rgb565[2 * i + 1]);
            CHECK(0);
        }
    }
}

/* Every Y/Cb/Cr combination, to aligned and unaligned destinations. */
static void test_all_colours(void)
{
    enum { WIDTH = 512 };
    static uint8_t planes[WIDTH * 3], rgb[WIDTH * 2 * 3];
    static struct { uint8_t before[16], data[WIDTH * 4 + 1], after[16]; } output;
    plm_frame_t frame = {0};
    frame.width = WIDTH; frame.height = 2;
    frame.y.width = WIDTH; frame.y.height = 2; frame.y.data = planes;
    frame.cb.width = frame.cr.width = WIDTH / 2;
    frame.cb.height = frame.cr.height = 1;
    frame.cb.data = planes + 2 * WIDTH;
    frame.cr.data = frame.cb.data + WIDTH / 2;
    for (unsigned cr = 0; cr < 256; ++cr) {
        for (unsigned y = 0; y < 256; y += 4) {
            for (unsigned i = 0; i < WIDTH / 2; ++i) {
                planes[2 * i] = y; planes[2 * i + 1] = y + 1;
                planes[WIDTH + 2 * i] = y + 2; planes[WIDTH + 2 * i + 1] = y + 3;
                frame.cb.data[i] = i; frame.cr.data[i] = cr;
            }
            plm_frame_to_rgb(&frame, rgb, WIDTH * 3);
            for (unsigned offset = 0; offset < 2; ++offset) {
                memset(&output, 0xa5, sizeof(output));
                uint8_t *out = output.data + offset;
                mpeg_yuv420_rows_to_rgb565(planes, planes + WIDTH, frame.cb.data, frame.cr.data,
                                           out, out + 2 * WIDTH, WIDTH);
                expect(rgb, out, WIDTH * 2);
                for (unsigned i = 0; i < 16; ++i)
                    CHECK(output.before[i] == 0xa5 && output.after[i] == 0xa5);
                CHECK(offset || output.data[WIDTH * 4] == 0xa5);
            }
        }
    }
}

/* Whole-frame helper with padded plane strides, a padded output stride and an
 * odd height. */
static void test_frame(void)
{
    enum { WIDTH = 20, HEIGHT = 7, Y_STRIDE = 32, C_STRIDE = 16, OUT_STRIDE = 48 };
    static uint8_t y[Y_STRIDE * (HEIGHT + 1)], cb[C_STRIDE * 4], cr[C_STRIDE * 4];
    static uint8_t rgb[WIDTH * (HEIGHT + 1) * 3], out[OUT_STRIDE * HEIGHT];
    for (unsigned i = 0; i < sizeof(y); ++i) y[i] = (uint8_t)(i * 37 + 5);
    for (unsigned i = 0; i < sizeof(cb); ++i) { cb[i] = (uint8_t)(i * 53 + 9); cr[i] = (uint8_t)(255 - i * 71); }
    plm_frame_t reference = {0};
    reference.width = WIDTH; reference.height = HEIGHT + 1;
    reference.y.width = Y_STRIDE; reference.y.height = HEIGHT + 1; reference.y.data = y;
    reference.cb.width = reference.cr.width = C_STRIDE;
    reference.cb.height = reference.cr.height = 4;
    reference.cb.data = cb; reference.cr.data = cr;
    plm_frame_to_rgb(&reference, rgb, WIDTH * 3);

    mpeg_frame_t frame = { WIDTH, HEIGHT, 0, {y, cb, cr}, {Y_STRIDE, C_STRIDE, C_STRIDE} };
    memset(out, 0xa5, sizeof(out));
    mpeg_frame_to_rgb565(&frame, out, OUT_STRIDE);
    for (unsigned row = 0; row < HEIGHT; ++row) {
        expect(rgb + row * WIDTH * 3, out + row * OUT_STRIDE, WIDTH);
        for (unsigned i = WIDTH * 2; i < OUT_STRIDE; ++i) CHECK(out[row * OUT_STRIDE + i] == 0xa5);
    }
}

/* Include SIMD block edges, odd widths and every input/output byte alignment.
 * The public row helper rounds odd widths up; padding is explicitly supplied. */
static void test_widths_and_alignment(void)
{
    enum { STRIDE = 48, OUT_STRIDE = 96 };
    static uint8_t planes[4 * STRIDE + 4], rgb[STRIDE * 2 * 3];
    static uint8_t out[OUT_STRIDE * 2 + 8];
    for (unsigned i = 0; i < sizeof(planes); ++i) planes[i] = (uint8_t)(i * 73 + 19);
    for (unsigned width = 1; width <= 35; ++width) {
        unsigned rounded = (width + 1) & ~1u;
        for (unsigned input_offset = 0; input_offset < 4; ++input_offset) {
            plm_frame_t reference = {0};
            reference.width = rounded; reference.height = 2;
            reference.y.width = STRIDE; reference.y.height = 2;
            reference.cb.width = reference.cr.width = STRIDE;
            reference.cb.height = reference.cr.height = 1;
            reference.y.data = planes + input_offset;
            reference.cb.data = planes + 2 * STRIDE + input_offset;
            reference.cr.data = planes + 3 * STRIDE + input_offset;
            plm_frame_to_rgb(&reference, rgb, rounded * 3);
            for (unsigned output_offset = 0; output_offset < 4; ++output_offset) {
                memset(out, 0xa5, sizeof(out));
                uint8_t *destination = out + 4 + output_offset;
                mpeg_yuv420_rows_to_rgb565(reference.y.data, reference.y.data + STRIDE,
                    reference.cb.data, reference.cr.data,
                    destination, destination + OUT_STRIDE, width);
                expect(rgb, destination, rounded);
                expect(rgb + rounded * 3, destination + OUT_STRIDE, rounded);
                for (unsigned i = 0; i < 4 + output_offset; ++i) CHECK(out[i] == 0xa5);
                for (unsigned row = 0; row < 2; ++row)
                    for (unsigned i = 2 * rounded; i < OUT_STRIDE; ++i)
                        CHECK(destination[row * OUT_STRIDE + i] == 0xa5);
            }
        }
    }
}

#ifdef __xcore__
static void benchmark(void)
{
    enum { WIDTH = 320, FRAMES = 32, ROW_PAIRS = 120 };
    static uint8_t planes[WIDTH * 3] __attribute__((aligned(8)));
    static uint8_t output[WIDTH * 4] __attribute__((aligned(8)));
    for (unsigned i = 0; i < sizeof(planes); ++i) planes[i] = (uint8_t)(i * 37 + 5);
    unsigned start = get_reference_time();
    for (unsigned frame = 0; frame < FRAMES; ++frame)
        for (unsigned pair = 0; pair < ROW_PAIRS; ++pair)
            mpeg_yuv420_rows_to_rgb565(planes, planes + WIDTH, planes + 2 * WIDTH,
                planes + 2 * WIDTH + WIDTH / 2, output, output + WIDTH * 2, WIDTH);
    unsigned ticks = get_reference_time() - start;
    unsigned hash = 2166136261u;
    for (unsigned i = 0; i < sizeof(output); ++i) hash = (hash ^ output[i]) * 16777619u;
    printf("RGB565 benchmark: %u us/frame, %u frames, checksum=%08x (conversion only)\n",
           ticks / (100 * FRAMES), FRAMES, hash);
}
#endif

int main(void)
{
    setbuf(stdout, NULL);
    puts("Checking RGB565 conversion against upstream PL_MPEG...");
    test_all_colours();
    test_frame();
    test_widths_and_alignment();
    puts("PASS: RGB565 conversion matches upstream PL_MPEG for all 16777216 colours, padded/odd frames and alignment/tail cases");
#ifdef __xcore__
    benchmark();
#endif
    return 0;
}
