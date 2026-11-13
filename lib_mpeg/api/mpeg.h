// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#ifndef LIB_MPEG_VIDEO_H_
#define LIB_MPEG_VIDEO_H_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Experimental PL_MPEG video adapter. One decoder on one task per tile;
 * no heap allocation and no callbacks. Not yet a cross-backend ABI.
 * Only fixed-resolution MPEG-1 elementary streams are accepted, not .mpg.
 * The optional libmpeg2 backend retains its separate mpeg2.h API. */
enum { MPEG_MAX_WIDTH = 320, MPEG_MAX_HEIGHT = 240,
       MPEG_INPUT_CAPACITY = 32768, MPEG_ARENA_CAPACITY = 384 * 1024 };
typedef enum {
    MPEG_ERROR = -1, MPEG_UNSUPPORTED = -2, MPEG_CAPACITY = -3,
    MPEG_NEED_INPUT = 0, MPEG_FRAME = 1, MPEG_END = 2
} mpeg_result_t;
typedef struct {
    unsigned width, height, number;
    /* Y, Cb, Cr in that order. Each plane has its own byte stride. These
     * tile-local pointers are read-only and valid until the next decode/close. */
    const uint8_t *plane[3];
    unsigned stride[3];
} mpeg_frame_t;
typedef struct { size_t peak, live, capacity; int guards_ok; } mpeg_memory_stats_t;

/* Open/reset an empty decoder. A second open without close is an error. */
mpeg_result_t mpeg_open(void);
/* Feed after NEED_INPUT, at most mpeg_input_space() bytes; data is copied.
 * end_of_input is final: drain mpeg_next() through MPEG_END, then close.
 * Oversized feeds are rejected without changing the decoder or taking ownership.
 * Backend parse/format errors are terminal; close before reopening. */
mpeg_result_t mpeg_feed(const uint8_t *data, size_t size, int end_of_input);
size_t mpeg_input_space(void);
mpeg_result_t mpeg_next(mpeg_frame_t *frame);
void mpeg_close(void);
mpeg_memory_stats_t mpeg_memory_stats(void);
const char *mpeg_backend_name(void);
#ifdef __cplusplus
}
#endif
#endif
