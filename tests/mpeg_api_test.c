// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpeg.h"
#include "mpeg_audio.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static uint8_t memory[MPEG_VIDEO_MEMORY_SIZE] __attribute__((aligned(8)));
static uint8_t audio_memory[MPEG_AUDIO_MEMORY_SIZE] __attribute__((aligned(8)));

static void rejected_audio(const uint8_t *data, size_t size)
{
    mpeg_audio_frame_t frame;
    CHECK(mpeg_audio_open(audio_memory, sizeof(audio_memory)) == MPEG_NEED_INPUT);
    CHECK(mpeg_audio_feed(data, size) == MPEG_NEED_INPUT);
    CHECK(mpeg_audio_next(&frame) == MPEG_ERROR);
    CHECK(mpeg_audio_finish() == MPEG_ERROR);
    mpeg_audio_close();
    CHECK(mpeg_audio_memory_stats().guards_ok && !mpeg_audio_memory_stats().live);
}

static void audio_api(void)
{
    static const uint8_t bad[][6] = {
        {0, 0, 0, 0, 0, 0},             /* No sync */
        {0xff, 0xfd, 0x00, 0, 0, 0},    /* Free-format/invalid bitrate */
        {0xff, 0xfd, 0xf4, 0, 0, 0},    /* Reserved bitrate */
        {0xff, 0xfd, 0x9c, 0, 0, 0},    /* Reserved sample rate */
        {0xff, 0xfb, 0x94, 0, 0, 0},    /* Layer III */
        {0xff, 0xf5, 0x94, 0, 0, 0},    /* MPEG-2 */
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) rejected_audio(bad[i], sizeof(bad[i]));
    /* A valid 48-kHz, 192-kbit/s header with missing frame payload must not
     * decode, and finish must report truncation even after consuming header. */
    static const uint8_t short_frame[] = {0xff, 0xfd, 0xa4, 0, 0, 0};
    mpeg_audio_frame_t frame;
    CHECK(!mpeg_audio_input_space());
    CHECK(mpeg_audio_open(NULL, sizeof(audio_memory)) == MPEG_ERROR);
    CHECK(mpeg_audio_open(audio_memory + 1, sizeof(audio_memory) - 1) == MPEG_ERROR);
    CHECK(mpeg_audio_open(audio_memory, 1024) == MPEG_CAPACITY);
    CHECK(mpeg_audio_open(audio_memory, sizeof(audio_memory)) == MPEG_NEED_INPUT);
    CHECK(mpeg_audio_open(audio_memory, sizeof(audio_memory)) == MPEG_ERROR);
    CHECK(mpeg_audio_input_space() == MPEG_AUDIO_INPUT_CAPACITY);
    CHECK(mpeg_audio_feed(short_frame, sizeof(short_frame)) == MPEG_NEED_INPUT);
    CHECK(mpeg_audio_next(&frame) == MPEG_NEED_INPUT);
    CHECK(mpeg_audio_next(NULL) == MPEG_ERROR);
    CHECK(mpeg_audio_finish() == MPEG_ERROR);
    mpeg_audio_close();
    CHECK(mpeg_audio_feed(short_frame, sizeof(short_frame)) == MPEG_ERROR);
    CHECK(mpeg_audio_next(&frame) == MPEG_ERROR);
    CHECK(mpeg_audio_open(audio_memory, sizeof(audio_memory)) == MPEG_NEED_INPUT);
    CHECK(mpeg_audio_feed(short_frame, MPEG_AUDIO_INPUT_CAPACITY + 1) == MPEG_CAPACITY);
    CHECK(mpeg_audio_input_space() == MPEG_AUDIO_INPUT_CAPACITY);
    CHECK(mpeg_audio_feed(NULL, 1) == MPEG_ERROR);
    CHECK(mpeg_audio_feed(short_frame, 0) == MPEG_ERROR);
    /* An audio decoder may be open alongside the video decoder. */
    CHECK(mpeg_open(memory, sizeof(memory)) == MPEG_NEED_INPUT);
    mpeg_audio_close();
    mpeg_close();
    CHECK(mpeg_audio_memory_stats().guards_ok && !mpeg_audio_memory_stats().live);
    CHECK(mpeg_memory_stats().guards_ok && !mpeg_memory_stats().live);
}

/* Memory that holds the decoder but not this stream's pictures is refused
 * when the sequence header arrives, before upstream allocates them. */
static void small_memory(const char *path)
{
    static unsigned char input[4096];
    FILE *file = fopen(path, "rb");
    CHECK(file);
    size_t size = fread(input, 1, sizeof(input), file);
    CHECK(size >= 8 && !fclose(file));
    CHECK(mpeg_open(memory, MPEG_INPUT_CAPACITY + 8192) == MPEG_NEED_INPUT);
    CHECK(mpeg_feed(input, size, 0) == MPEG_CAPACITY);
    mpeg_frame_t frame;
    CHECK(mpeg_next(&frame) == MPEG_CAPACITY);
    mpeg_close();
    CHECK(mpeg_memory_stats().guards_ok && !mpeg_memory_stats().live);
}

static void repeat_decode(const char *path, unsigned rate_num, unsigned rate_den, unsigned cycles)
{
    static unsigned char input[4096];
    uint32_t baseline = 0;
    for (unsigned cycle = 0; cycle < cycles; ++cycle) {
        FILE *file = fopen(path, "rb");
        CHECK(file);
        CHECK(mpeg_open(memory, sizeof(memory)) == MPEG_NEED_INPUT);
        unsigned frames = 0;
        uint32_t hash = UINT32_C(2166136261);
        while (1) {
            mpeg_frame_t frame;
            mpeg_result_t result = mpeg_next(&frame);
            if (result == MPEG_END) break;
            if (result == MPEG_FRAME) {
                CHECK(frame.number == frames++);
                CHECK(frame.frame_rate_num == rate_num && frame.frame_rate_den == rate_den);
                for (unsigned p = 0; p < 3; ++p) {
                    unsigned w = p ? (frame.width + 1) / 2 : frame.width;
                    unsigned h = p ? (frame.height + 1) / 2 : frame.height;
                    for (unsigned y = 0; y < h; ++y)
                        for (unsigned x = 0; x < w; ++x)
                            hash = (hash ^ frame.plane[p][y * frame.stride[p] + x]) * UINT32_C(16777619);
                }
            } else {
                CHECK(result == MPEG_NEED_INPUT);
                size_t size = mpeg_input_space();
                unsigned chunk = cycle & 1 ? 4093 : 4096;
                if (size > chunk) size = chunk;
                CHECK(size);
                size = fread(input, 1, size, file);
                CHECK(!ferror(file));
                CHECK(mpeg_feed(input, size, size == 0) == MPEG_NEED_INPUT);
            }
        }
        CHECK(frames == 24);
        if (!cycle) baseline = hash;
        CHECK(hash == baseline);
        CHECK(mpeg_memory_stats().guards_ok);
        mpeg_close();
        CHECK(!mpeg_memory_stats().live);
        CHECK(!fclose(file));
    }
}

int main(int argc, char **argv)
{
    if (argc == 5 && !strcmp(argv[1], "--rate")) {
        repeat_decode(argv[2], (unsigned)strtoul(argv[3], NULL, 10),
                      (unsigned)strtoul(argv[4], NULL, 10), 1);
        puts("PASS: sequence-header nominal frame rate");
        return 0;
    }
    CHECK(!mpeg_input_space());
    CHECK(mpeg_next(NULL) == MPEG_ERROR);
    CHECK(mpeg_open(NULL, sizeof(memory)) == MPEG_ERROR);
    CHECK(mpeg_open(memory + 4, sizeof(memory) - 4) == MPEG_ERROR);
    CHECK(mpeg_open(memory, MPEG_INPUT_CAPACITY) == MPEG_CAPACITY);
    audio_api();
    for (unsigned i = 0; i < 100; ++i) {
        CHECK(mpeg_open(memory, sizeof(memory)) == MPEG_NEED_INPUT);
        CHECK(mpeg_open(memory, sizeof(memory)) == MPEG_ERROR);
        size_t space = mpeg_input_space();
        CHECK(space == MPEG_INPUT_CAPACITY);
        CHECK(mpeg_feed((const uint8_t *)"x", MPEG_INPUT_CAPACITY + 1, 0) == MPEG_CAPACITY);
        CHECK(mpeg_input_space() == space);
        CHECK(mpeg_feed(NULL, 1, 0) == MPEG_ERROR);
        CHECK(mpeg_input_space() == space);
        CHECK(mpeg_feed(NULL, 0, 1) == MPEG_NEED_INPUT);
        CHECK(!mpeg_input_space());
        mpeg_frame_t frame;
        CHECK(mpeg_next(&frame) == MPEG_ERROR);
        mpeg_close();
        CHECK(mpeg_memory_stats().guards_ok && !mpeg_memory_stats().live);
    }
    /* Reserved rate codes are rejected even when the header arrives bytewise. */
    for (unsigned rate = 0; rate < 16; ++rate) {
        if (rate >= 1 && rate <= 8) continue;
        uint8_t header[] = {0, 0, 1, 0xb3, 0x14, 0x00, 0xf0, (uint8_t)(0x10 | rate)};
        CHECK(mpeg_open(memory, sizeof(memory)) == MPEG_NEED_INPUT);
        for (unsigned i = 0; i < sizeof(header); ++i)
            CHECK(mpeg_feed(header + i, 1, 0) == (i == 7 ? MPEG_UNSUPPORTED : MPEG_NEED_INPUT));
        mpeg_close();
    }
    if (argc == 2) { small_memory(argv[1]); repeat_decode(argv[1], 25, 1, 30); }
    puts("PASS: PL_MPEG video and audio API, supplied memory, bounded feed, reopen, guards and cleanup");
    return 0;
}
