// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdio.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    FILE *output = fopen(argv[2], "wb");
    if (!output) return 2;
    for (unsigned play = 0; play < 2; ++play) {
        plm_buffer_t *input = plm_buffer_create_with_filename(argv[1]);
        if (!input) { fclose(output); return 2; }
        plm_audio_t *decoder = plm_audio_create_with_buffer(input, 1);
        unsigned blocks = 0;
        plm_samples_t *samples;
        while ((samples = plm_audio_decode(decoder))) {
            if (fwrite(samples->interleaved, sizeof(float), samples->count * 2, output) != samples->count * 2)
                return 1;
            ++blocks;
        }
        printf("Reference: play=%u blocks=%u\n", play, blocks);
        if (!blocks) return 1;
        plm_audio_destroy(decoder);
    }
    return fclose(output);
}
