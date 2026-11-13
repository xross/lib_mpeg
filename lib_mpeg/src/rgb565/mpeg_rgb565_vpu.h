// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#ifndef MPEG_RGB565_VPU_H_
#define MPEG_RGB565_VPU_H_
#include <stddef.h>
#include <stdint.h>

/* First 16 bytes of each vector contain signed, recentered samples. The second
 * 16 bytes are readable padding for 256-bit loads, not extra input samples.
 * Cb/Cr are horizontally duplicated; the assembly reuses them for both rows. */
typedef struct {
    uint32_t y0[8], y1[8], cb[8], cr[8];
} mpeg_rgb565_vpu_input_t;

_Static_assert(sizeof(mpeg_rgb565_vpu_input_t) == 128 &&
               offsetof(mpeg_rgb565_vpu_input_t, y1) == 32 &&
               offsetof(mpeg_rgb565_vpu_input_t, cb) == 64 &&
               offsetof(mpeg_rgb565_vpu_input_t, cr) == 96,
               "VPU input must match the assembly's four 32-byte vectors");

void mpeg_rgb565_vpu_block(const mpeg_rgb565_vpu_input_t *input,
                           uint8_t *out0, uint8_t *out1);
#endif
