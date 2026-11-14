// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
/* Native-only oracle. Intentionally uses pristine upstream/default desktop
 * allocation and stdio, never compiled into the xcore library or benchmark. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

static int write_frame(FILE *output, const plm_frame_t *frame)
{
    const plm_plane_t *planes[3] = {&frame->y, &frame->cb, &frame->cr};
    for (unsigned p = 0; p < 3; ++p) {
        unsigned w = p ? (frame->width + 1) / 2 : frame->width;
        unsigned h = p ? (frame->height + 1) / 2 : frame->height;
        for (unsigned y = 0; y < h; ++y)
            if (fwrite(planes[p]->data + y * planes[p]->width, 1, w, output) != w) return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3 && (argc != 4 || strcmp(argv[3], "--drain"))) return 1;
    FILE *output = fopen(argv[2], "wb");
    plm_buffer_t *input = plm_buffer_create_with_filename(argv[1]);
    if (!output || !input) return 1;
    plm_video_t *decoder = plm_video_create_with_buffer(input, 1);
    plm_frame_t *frame;
    unsigned frames = 0;
    int failed = 0;
    while ((frame = plm_video_decode(decoder))) {
        if (write_frame(output, frame)) failed = 1;
        ++frames;
    }
    /* Explicit opt-in for full-sequence quality comparisons. The default
     * oracle preserves pristine upstream's missing-final-anchor behaviour. */
    if (argc == 4 && decoder->has_reference_frame && decoder->picture_type == PLM_VIDEO_PICTURE_TYPE_B) {
        if (write_frame(output, &decoder->frame_backward)) failed = 1;
        ++frames;
    }
    plm_video_destroy(decoder);
    if (fclose(output)) failed = 1;
    printf("Pristine PL_MPEG: frames=%u\n", frames);
    return failed || !frames;
}
