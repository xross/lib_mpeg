// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#ifndef LIB_MPEG_RGB565_H_
#define LIB_MPEG_RGB565_H_
#include <stdint.h>
#include "mpeg.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Colour conversion from decoded pictures to RGB565, for displays that take
 * 16 bit pixels. Available with the PL_MPEG backend only.
 *
 * Input is limited-range BT.601 YUV 4:2:0, as decoded. Output pixels are two
 * bytes each, most significant byte first (RRRRRGGG GGGBBBBB), which is the
 * order SPI display controllers such as the ST7789 expect. Results are
 * identical to PL_MPEG's own RGB conversion followed by truncation to 5/6/5
 * bits. No state is shared with the decoder, so these may be called on a
 * different tile from the one that decodes. XS3 builds use the VPU by default;
 * MPEG_RGB565_VPU 0 in mpeg_conf.h selects the scalar lookup-table implementation.
 * Only scalar table initialisation is non-reentrant: make its first call from one
 * task. The VPU implementation uses task-local stack scratch, with no tables. */

/* Convert two picture rows that share one row of chroma.
 *   y0, y1      width luma samples each (the upper and lower row)
 *   cb, cr      (width + 1) / 2 chroma samples each
 *   out0, out1  2 * width bytes each, rounded up to an even width
 * An odd width is rounded up: one extra luma sample is read from each row and
 * one extra pixel written to each output. The VPU fast path handles blocks of
 * 16 pixels when all input and output pointers are 4-byte aligned. Other
 * alignments and remaining pixels use an exact scalar fallback. */
void mpeg_yuv420_rows_to_rgb565(const uint8_t *y0, const uint8_t *y1,
                                const uint8_t *cb, const uint8_t *cr,
                                uint8_t *out0, uint8_t *out1, unsigned width);

/* Convert a whole decoded frame. `stride` is the number of bytes from one
 * output row to the next and must be at least 2 * frame->width, rounded up to
 * an even width. The frame is only read. */
void mpeg_frame_to_rgb565(const mpeg_frame_t *frame, uint8_t *destination, unsigned stride);

#ifdef __cplusplus
}
#endif
#endif
