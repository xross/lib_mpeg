// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#ifndef LIB_MPEG_AUDIO_H_
#define LIB_MPEG_AUDIO_H_
#include <stddef.h>
#include <stdint.h>
#include "mpeg.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Experimental MPEG-1 Audio Layer II (MP2) decoder, from PL_MPEG. Available
 * with the PL_MPEG backend only. One audio decoder per tile, driven from one
 * task, which may be a different task or tile from the video decoder. No heap
 * allocation and no callbacks.
 *
 * Input is an MP2 elementary stream: the application demultiplexes a program
 * stream itself. The stream must start on a frame header and keep one format;
 * there is no resynchronisation after a damaged frame. Output pacing and
 * audio/video synchronisation are left to the application. */
enum { MPEG_AUDIO_INPUT_CAPACITY = 4096, MPEG_AUDIO_MEMORY_SIZE = 28672,
       MPEG_AUDIO_FRAME_SAMPLES = 1152, MPEG_AUDIO_CHANNELS = 2 };
typedef struct {
    /* MPEG_AUDIO_FRAME_SAMPLES pairs of left, right in the range -1 to 1; a
     * mono stream is given on both. Valid until the next call for this decoder. */
    const float *samples;
    unsigned sample_rate;
} mpeg_audio_frame_t;

/* Open an empty decoder using size bytes at memory: at least
 * MPEG_AUDIO_MEMORY_SIZE, aligned to eight bytes and untouched by the
 * application until mpeg_audio_close(). Returns MPEG_NEED_INPUT; a second open
 * without close, or null or misaligned memory, is MPEG_ERROR, and memory that
 * is too small is MPEG_CAPACITY. */
mpeg_result_t mpeg_audio_open(void *memory, size_t size);
/* Feed between 1 and mpeg_audio_input_space() bytes; data is copied. Returns
 * MPEG_NEED_INPUT, MPEG_CAPACITY for an oversized feed, which changes nothing,
 * or MPEG_ERROR. */
mpeg_result_t mpeg_audio_feed(const uint8_t *data, size_t size);
size_t mpeg_audio_input_space(void);
/* MPEG_FRAME with one decoded frame, MPEG_NEED_INPUT when a whole frame is not
 * yet buffered, or MPEG_ERROR for a malformed or unsupported stream. Errors are
 * terminal; close before reopening. */
mpeg_result_t mpeg_audio_next(mpeg_audio_frame_t *frame);
/* After the last feed and draining mpeg_audio_next(): MPEG_END if the stream
 * ended on a frame boundary, MPEG_ERROR if it failed or was truncated. */
mpeg_result_t mpeg_audio_finish(void);
void mpeg_audio_close(void);
mpeg_memory_stats_t mpeg_audio_memory_stats(void);
#ifdef __cplusplus
}
#endif
#endif
