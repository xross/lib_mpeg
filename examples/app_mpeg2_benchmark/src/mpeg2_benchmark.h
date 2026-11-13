/* Copyright 2026 XMOS LIMITED.
 * SPDX-License-Identifier: MIT */
#ifndef MPEG2_BENCHMARK_H_
#define MPEG2_BENCHMARK_H_
/* Progressive MPEG-2 4:2:0 elementary stream, up to 320x240. Output is optional
 * native-only planar YUV for reference checks, not a proposed playback format. */
int mpeg2_benchmark(const char *input_path, const char *output_path, unsigned read_size);
#endif
