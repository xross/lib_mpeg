// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdio.h>
#include <stdlib.h>
#include "mpeg_audio.h"
static unsigned char memory[MPEG_AUDIO_MEMORY_SIZE] __attribute__((aligned(8)));

/* Elementary MP2 only: no application, filesystem driver or demux dependency. */
static int decode(FILE *source, FILE *output, unsigned read_size, unsigned play)
{
    unsigned char data[MPEG_AUDIO_INPUT_CAPACITY];
    unsigned blocks = 0, sample_rate = 0;
    int failed = 0;
    if (mpeg_audio_open(memory, sizeof(memory)) != MPEG_NEED_INPUT) return 1;
    for (;;) {
        mpeg_audio_frame_t frame;
        mpeg_result_t result = mpeg_audio_next(&frame);
        if (result == MPEG_FRAME) {
            sample_rate = frame.sample_rate;
            unsigned samples = MPEG_AUDIO_FRAME_SAMPLES * MPEG_AUDIO_CHANNELS;
            if (fwrite(frame.samples, sizeof(float), samples, output) != samples) { failed = 1; break; }
            ++blocks;
        } else if (result == MPEG_NEED_INPUT) {
            size_t count = mpeg_audio_input_space();
            if (count > read_size) count = read_size;
            if (!count) { failed = 1; break; }
            count = fread(data, 1, count, source);
            if (ferror(source)) { failed = 1; break; }
            if (!count) {
                failed = mpeg_audio_finish() != MPEG_END || !blocks;
                break;
            }
            if (mpeg_audio_feed(data, count) != MPEG_NEED_INPUT) { failed = 1; break; }
        } else {
            failed = 1;
            break;
        }
    }
    mpeg_memory_stats_t stats = mpeg_audio_memory_stats();
    if (!stats.guards_ok) failed = 1;
    mpeg_audio_close();
    if (mpeg_audio_memory_stats().live) failed = 1;
    if (!failed)
        printf("PASS: audio play=%u blocks=%u rate=%u samples=%u arena=%u\n",
               play, blocks, sample_rate, blocks * MPEG_AUDIO_FRAME_SAMPLES, (unsigned)stats.peak);
    return failed;
}

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4) return 2;
    unsigned read_size = argc == 4 ? (unsigned)strtoul(argv[3], NULL, 10) : 4096;
    if (!read_size || read_size > MPEG_AUDIO_INPUT_CAPACITY) return 2;
    FILE *source = fopen(argv[1], "rb"), *output = fopen(argv[2], "wb");
    if (!source || !output) {
        if (source) fclose(source);
        if (output) fclose(output);
        return 2;
    }
    int failed = 0;
    for (unsigned play = 0; play < 2; ++play) {
        if (fseek(source, 0, SEEK_SET) || decode(source, output, read_size, play)) { failed = 1; break; }
    }
    if (fclose(source)) failed = 1;
    if (fclose(output)) failed = 1;
    return failed;
}
