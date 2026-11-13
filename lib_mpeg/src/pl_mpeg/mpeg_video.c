// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stddef.h>
#include <stdint.h>
#include "mpeg.h"
#include "mpeg_memory.h"

#define PLM_NO_STDIO
#define PLM_BUFFER_DEFAULT_SIZE MPEG_INPUT_CAPACITY
#define PLM_MALLOC(size) mpeg_memory_alloc(size)
#define PLM_REALLOC(ptr, size) mpeg_memory_resize(ptr, size)
#define PLM_FREE(ptr) mpeg_memory_free(ptr)
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

static plm_buffer_t *buffer;
static plm_video_t *video;
static unsigned ended, frames, checked, have_picture, prefix_size;
static unsigned width, height;
static uint8_t prefix[8];
static uint32_t scanner;
static mpeg_result_t failure;

const char *mpeg_backend_name(void) { return "PL_MPEG"; }

mpeg_result_t mpeg_open(void)
{
    if (video) return MPEG_ERROR;
    mpeg_memory_reset();
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
            checked = 1;
        }
        scanner = (scanner << 8) | byte;
        if (scanner == UINT32_C(0x000001b5)) return failure = MPEG_UNSUPPORTED;
        if (scanner == UINT32_C(0x00000100)) have_picture = 1;
    }
    if (size) plm_buffer_write(buffer, (uint8_t *)data, size);
    if (end_of_input) {
        ended = 1;
        plm_buffer_signal_end(buffer);
        /* Upstream uses total_size==0 to mean an unknown length. A zero-byte
         * final feed after all bytes were consumed must still signal EOF. */
        if (!buffer->length) buffer->has_ended = 1;
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
    plm_frame_t *decoded = plm_video_decode(video);
    /* At the pinned upstream revision, EOF after a coded B picture fails to
     * release the final display-order anchor. Keep this workaround in the
     * adapter, not the pristine submodule; reference tests check all pictures. */
    if (!decoded && ended && video->has_reference_frame &&
        video->picture_type == PLM_VIDEO_PICTURE_TYPE_B) {
        decoded = &video->frame_backward;
        video->has_reference_frame = 0;
    }
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
    if (video) plm_video_destroy(video);
    video = NULL;
    buffer = NULL;
}
