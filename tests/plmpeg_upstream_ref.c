// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
/* Native-only oracle. Intentionally uses pristine upstream/default desktop
 * allocation and stdio, never compiled into the xcore library or benchmark. */
#include <stddef.h>
#include <stdio.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    FILE *output = fopen(argv[2], "wb");
    plm_buffer_t *input = plm_buffer_create_with_filename(argv[1]);
    if (!output || !input) return 1;
    plm_video_t *decoder = plm_video_create_with_buffer(input, 1);
    plm_frame_t *frame;
    unsigned frames = 0;
    int failed = 0;
    while ((frame = plm_video_decode(decoder))) {
        const plm_plane_t *planes[3] = {&frame->y, &frame->cb, &frame->cr};
        for (unsigned p = 0; p < 3; ++p) {
            unsigned w = p ? (frame->width + 1) / 2 : frame->width;
            unsigned h = p ? (frame->height + 1) / 2 : frame->height;
            for (unsigned y = 0; y < h; ++y)
                if (fwrite(planes[p]->data + y * planes[p]->width, 1, w, output) != w) failed = 1;
        }
        ++frames;
    }
    plm_video_destroy(decoder);
    if (fclose(output)) failed = 1;
    printf("Pristine PL_MPEG: frames=%u\n", frames);
    return failed || !frames;
}
