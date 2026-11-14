// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stddef.h>
#include <stdint.h>
#include "mpeg_config.h"
#include "mpeg.h"
#include "mpeg_audio.h"
#include "mpeg_memory.h"

#define PLM_NO_STDIO
#define PLM_BUFFER_DEFAULT_SIZE MPEG_INPUT_CAPACITY
#define PLM_MALLOC(size) mpeg_memory_alloc(size)
#define PLM_REALLOC(ptr, size) mpeg_memory_resize(ptr, size)
#define PLM_FREE(ptr) mpeg_memory_free(ptr)
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

/* Three pictures of whole macroblocks, luma plus two quarter-size chroma. */
#define VIDEO_PICTURES(width, height) \
    ((size_t)3 * ((((width) + 15) & ~15u) * (((height) + 15) & ~15u) * 3 / 2))
#define VIDEO_FIXED (MPEG_MEMORY_BLOCK(sizeof(plm_buffer_t)) + \
    MPEG_MEMORY_BLOCK(MPEG_INPUT_CAPACITY) + MPEG_MEMORY_BLOCK(sizeof(plm_video_t)))
typedef char video_memory_size[VIDEO_FIXED + MPEG_MEMORY_BLOCK(VIDEO_PICTURES(MPEG_MAX_WIDTH,
    MPEG_MAX_HEIGHT)) <= MPEG_VIDEO_MEMORY_SIZE ? 1 : -1];

static mpeg_arena_t arena;
static plm_buffer_t *buffer;
static plm_video_t *video;
static unsigned ended, frames, checked, have_picture, prefix_size;
static unsigned width, height;
static uint8_t prefix[8];
static uint32_t scanner;
static mpeg_result_t failure;

const char *mpeg_backend_name(void) { return "PL_MPEG"; }

mpeg_memory_stats_t mpeg_memory_stats(void) { return mpeg_memory_arena_stats(&arena); }

mpeg_result_t mpeg_open(void *memory, size_t size)
{
    if (video || !mpeg_memory_init(&arena, memory, size)) return MPEG_ERROR;
    if (size < VIDEO_FIXED) return MPEG_CAPACITY;
    mpeg_memory_select(&arena);
    ended = frames = checked = have_picture = prefix_size = 0;
    scanner = UINT32_C(0xffffffff);
    failure = MPEG_NEED_INPUT;
    buffer = plm_buffer_create_with_capacity(MPEG_INPUT_CAPACITY);
    video = plm_video_create_with_buffer(buffer, 1);
    return MPEG_NEED_INPUT;
}

size_t mpeg_input_space(void)
{
    if (!video || ended || failure < 0) return 0;
    return MPEG_INPUT_CAPACITY - plm_buffer_get_remaining(buffer);
}

mpeg_result_t mpeg_feed(const uint8_t *data, size_t size, int end_of_input)
{
    if (!video || ended || (size && !data)) return MPEG_ERROR;
    if (failure < 0) return failure;
    if (size > mpeg_input_space()) return MPEG_CAPACITY;
    /* Preflight before upstream's first picture allocation. Require a sequence
     * header at offset zero, bounded dimensions and a valid frame-rate code.
     * MPEG-2 extensions before the first picture are explicitly rejected. */
    for (size_t i = 0; i < size && !have_picture; ++i) {
        uint8_t byte = data[i];
        if (prefix_size < sizeof(prefix)) prefix[prefix_size++] = byte;
        if (!checked && prefix_size == sizeof(prefix)) {
            width = ((unsigned)prefix[4] << 4) | (prefix[5] >> 4);
            height = ((unsigned)(prefix[5] & 15) << 8) | prefix[6];
            unsigned rate = prefix[7] & 15;
            if (prefix[0] || prefix[1] || prefix[2] != 1 || prefix[3] != 0xb3 ||
                !width || !height || width > MPEG_MAX_WIDTH || height > MPEG_MAX_HEIGHT ||
                !rate || rate > 8) return failure = MPEG_UNSUPPORTED;
            /* Upstream allocates the pictures unchecked on this header. */
            if (MPEG_MEMORY_BLOCK(VIDEO_PICTURES(width, height)) > mpeg_memory_room(&arena))
                return failure = MPEG_CAPACITY;
            checked = 1;
        }
        scanner = (scanner << 8) | byte;
        if (scanner == UINT32_C(0x000001b5)) return failure = MPEG_UNSUPPORTED;
        if (scanner == UINT32_C(0x00000100)) have_picture = 1;
    }
    mpeg_memory_select(&arena);
    if (size) plm_buffer_write(buffer, (uint8_t *)data, size);
    if (end_of_input) {
        ended = 1;
        plm_buffer_signal_end(buffer);
    }
    return MPEG_NEED_INPUT;
}

mpeg_result_t mpeg_next(mpeg_frame_t *frame)
{
    if (!video || !frame) return MPEG_ERROR;
    if (failure < 0) return failure;
    if (!checked || !have_picture) {
        if (ended) return failure = MPEG_ERROR;
        return MPEG_NEED_INPUT;
    }
    mpeg_memory_select(&arena);
    plm_frame_t *decoded = plm_video_decode(video);
    if (!decoded) {
        if (ended) {
            if (!frames) return failure = MPEG_ERROR;
            return MPEG_END;
        }
        return MPEG_NEED_INPUT;
    }
    if (decoded->width != width || decoded->height != height) return failure = MPEG_UNSUPPORTED;
    frame->width = width;
    frame->height = height;
    frame->number = frames++;
    frame->plane[0] = decoded->y.data;
    frame->plane[1] = decoded->cb.data;
    frame->plane[2] = decoded->cr.data;
    frame->stride[0] = decoded->y.width;
    frame->stride[1] = decoded->cb.width;
    frame->stride[2] = decoded->cr.width;
    return MPEG_FRAME;
}

void mpeg_close(void)
{
    mpeg_memory_select(&arena);
    if (video) plm_video_destroy(video);
    video = NULL;
    buffer = NULL;
}

/* MP2 audio shares this translation unit: PL_MPEG is a single header whose
 * implementation, and private state the adapters read, exist only here. */
#define AUDIO_FIXED (MPEG_MEMORY_BLOCK(sizeof(plm_buffer_t)) + \
    MPEG_MEMORY_BLOCK(MPEG_AUDIO_INPUT_CAPACITY) + MPEG_MEMORY_BLOCK(sizeof(plm_audio_t)))
typedef char audio_memory_size[AUDIO_FIXED <= MPEG_AUDIO_MEMORY_SIZE ? 1 : -1];
typedef char audio_frame_samples[MPEG_AUDIO_FRAME_SAMPLES == PLM_AUDIO_SAMPLES_PER_FRAME ? 1 : -1];

static mpeg_arena_t audio_arena;
static plm_buffer_t *audio_input;
static plm_audio_t *audio;
static int audio_failed;

mpeg_memory_stats_t mpeg_audio_memory_stats(void) { return mpeg_memory_arena_stats(&audio_arena); }

mpeg_result_t mpeg_audio_open(void *memory, size_t size)
{
    if (audio || !mpeg_memory_init(&audio_arena, memory, size)) return MPEG_ERROR;
    if (size < AUDIO_FIXED) return MPEG_CAPACITY;
    mpeg_memory_select(&audio_arena);
    audio_failed = 0;
    audio_input = plm_buffer_create_with_capacity(MPEG_AUDIO_INPUT_CAPACITY);
    audio = plm_audio_create_with_buffer(audio_input, 1);
    return MPEG_NEED_INPUT;
}

size_t mpeg_audio_input_space(void)
{
    if (!audio || audio_failed) return 0;
    return MPEG_AUDIO_INPUT_CAPACITY - plm_buffer_get_remaining(audio_input);
}

mpeg_result_t mpeg_audio_feed(const uint8_t *data, size_t size)
{
    if (!audio || audio_failed || !data || !size) return MPEG_ERROR;
    if (size > mpeg_audio_input_space()) return MPEG_CAPACITY;
    mpeg_memory_select(&audio_arena);
    plm_buffer_write(audio_input, (uint8_t *)data, size);
    return MPEG_NEED_INPUT;
}

mpeg_result_t mpeg_audio_next(mpeg_audio_frame_t *frame)
{
    if (!audio || audio_failed || !frame) return MPEG_ERROR;
    size_t remaining = plm_buffer_get_remaining(audio_input);
    if (!audio->next_frame_data_size && remaining >= 4) {
        const uint8_t *p = audio_input->bytes + (audio_input->bit_index >> 3);
        unsigned bitrate = p[2] >> 4, rate = (p[2] >> 2) & 3;
        /* Check before upstream indexes its quantizer/bitrate tables. This
         * adapter deliberately rejects resynchronisation and format changes. */
        if (p[0] != 0xff || (p[1] & 0xfe) != 0xfc || !bitrate || bitrate == 15 || rate == 3) {
            audio_failed = 1;
            return MPEG_ERROR;
        }
    }
    if (!audio->next_frame_data_size) {
        if (remaining < 6) return MPEG_NEED_INPUT;
        audio->next_frame_data_size = plm_audio_decode_header(audio);
        if (!audio->next_frame_data_size) {
            audio_failed = 1;
            return MPEG_ERROR;
        }
    }
    size_t frame_end = audio_input->bit_index + (size_t)audio->next_frame_data_size * 8;
    if (frame_end > audio_input->length * 8) return MPEG_NEED_INPUT;
    plm_samples_t *samples = plm_audio_decode(audio);
    if (!samples || audio_input->bit_index > frame_end) {
        audio_failed = 1;
        return MPEG_ERROR;
    }
    /* Do not mistake legitimate ancillary/stuffing bytes for another frame
     * header, and never resynchronise across a damaged frame boundary. */
    audio_input->bit_index = frame_end;
    frame->samples = samples->interleaved;
    frame->sample_rate = (unsigned)plm_audio_get_samplerate(audio);
    return MPEG_FRAME;
}

mpeg_result_t mpeg_audio_finish(void)
{
    return !audio || audio_failed || audio->next_frame_data_size ||
           plm_buffer_get_remaining(audio_input) ? MPEG_ERROR : MPEG_END;
}

void mpeg_audio_close(void)
{
    mpeg_memory_select(&audio_arena);
    if (audio) plm_audio_destroy(audio);
    audio = NULL;
    audio_input = NULL;
}
