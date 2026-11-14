// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#ifndef LIB_MPEG_VIDEO_H_
#define LIB_MPEG_VIDEO_H_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Experimental PL_MPEG video adapter. One video decoder per tile, driven from
 * one task; no heap allocation and no callbacks. Not yet a cross-backend ABI.
 * Only fixed-resolution MPEG-1 elementary streams are accepted, not .mpg.
 * MP2 audio is declared in mpeg_audio.h. The optional libmpeg2 backend retains
 * its separate mpeg2.h API.
 *
 * The application owns the decoder's memory and passes it to mpeg_open().
 * MPEG_VIDEO_MEMORY_SIZE is enough for any supported stream; a smaller picture
 * needs less (about 4.5 bytes per luma sample of the picture rounded up to
 * whole macroblocks, plus the input buffer and 2 KiB). */
enum { MPEG_MAX_WIDTH = 320, MPEG_MAX_HEIGHT = 240,
       MPEG_INPUT_CAPACITY = 32768, MPEG_VIDEO_MEMORY_SIZE = 384 * 1024 };
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
    /* Nominal frames/second from the sequence header, as an exact rational
     * (for example 30000/1001). This is source timing, not measured throughput. */
    unsigned frame_rate_num, frame_rate_den;
} mpeg_frame_t;
typedef struct { size_t peak, live, capacity; int guards_ok; } mpeg_memory_stats_t;

/* Open/reset an empty decoder using size bytes at memory, which must be
 * aligned to eight bytes and stay untouched by the application until
 * mpeg_close(). A second open without close, or null or misaligned memory, is
 * MPEG_ERROR. Memory too small for the decoder is MPEG_CAPACITY here; memory
 * too small for the stream's pictures is MPEG_CAPACITY from mpeg_feed(). */
mpeg_result_t mpeg_open(void *memory, size_t size);
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
