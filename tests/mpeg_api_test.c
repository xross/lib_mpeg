// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdio.h>
#include <stdlib.h>
#include "mpeg.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static void repeat_decode(const char *path)
{
    static unsigned char input[4096];
    uint32_t baseline = 0;
    for (unsigned cycle = 0; cycle < 30; ++cycle) {
        FILE *file = fopen(path, "rb");
        CHECK(file);
        CHECK(mpeg_open() == MPEG_NEED_INPUT);
        unsigned frames = 0;
        uint32_t hash = UINT32_C(2166136261);
        while (1) {
            mpeg_frame_t frame;
            mpeg_result_t result = mpeg_next(&frame);
            if (result == MPEG_END) break;
            if (result == MPEG_FRAME) {
                CHECK(frame.number == frames++);
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
    CHECK(!mpeg_input_space());
    CHECK(mpeg_next(NULL) == MPEG_ERROR);
    for (unsigned i = 0; i < 100; ++i) {
        CHECK(mpeg_open() == MPEG_NEED_INPUT);
        CHECK(mpeg_open() == MPEG_ERROR);
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
    if (argc == 2) repeat_decode(argv[1]);
    puts("PASS: PL_MPEG API, bounded feed, reopen, guards and allocation cleanup");
    return 0;
}
