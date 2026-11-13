// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdint.h>
#include <stdio.h>
#ifdef __xcore__
#include <xcore/hwtimer.h>
static hwtimer_t timer;
typedef uint32_t bench_time_t;
static bench_time_t now(void) { return hwtimer_get_time(timer); }
static uint64_t elapsed(bench_time_t start) { return (uint32_t)(now() - start) * UINT64_C(10); }
#else
#include <time.h>
typedef uint64_t bench_time_t;
static bench_time_t now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + t.tv_nsec;
}
static uint64_t elapsed(bench_time_t start) { return now() - start; }
#endif
#include "mpeg.h"
#include "plmpeg_benchmark.h"

static uint8_t input[4096];
static int output_frame(FILE *output, const mpeg_frame_t *frame, uint32_t *hash)
{
    for (unsigned p = 0; p < 3; ++p) {
        unsigned width = p ? (frame->width + 1) / 2 : frame->width;
        unsigned height = p ? (frame->height + 1) / 2 : frame->height;
        const uint8_t *row = frame->plane[p];
        for (unsigned y = 0; y < height; ++y, row += frame->stride[p]) {
            for (unsigned x = 0; x < width; ++x) *hash = (*hash ^ row[x]) * UINT32_C(16777619);
            if (output && fwrite(row, 1, width, output) != width) return 0;
        }
    }
    return 1;
}

int plmpeg_benchmark(const char *input_path, const char *output_path, unsigned read_size)
{
    if (!read_size || read_size > sizeof(input)) return 1;
    FILE *file = fopen(input_path, "rb");
    if (!file) { fprintf(stderr, "Cannot open %s\n", input_path); return 1; }
    FILE *output = output_path ? fopen(output_path, "wb") : NULL;
    if (output_path && !output) { fclose(file); return 1; }
    if (mpeg_open() != MPEG_NEED_INPUT) { fclose(file); if (output) fclose(output); return 1; }
#ifdef __xcore__
    timer = hwtimer_alloc();
    if (!timer) { mpeg_close(); fclose(file); if (output) fclose(output); return 1; }
#endif
    unsigned frames = 0, failed = 0;
    uint64_t decode_ns = 0, feed_ns = 0, worst_next_ns = 0;
    uint32_t hash = UINT32_C(2166136261);
    mpeg_result_t result = MPEG_NEED_INPUT;
    while (!failed) {
        mpeg_frame_t frame;
        bench_time_t start = now();
        result = mpeg_next(&frame);
        uint64_t duration = elapsed(start);
        decode_ns += duration;
        if (duration > worst_next_ns) worst_next_ns = duration;
        if (result == MPEG_END) break;
        if (result == MPEG_FRAME) {
            if (frame.number != frames || !output_frame(output, &frame, &hash)) failed = 1;
            if (!frames) printf("Sequence: %ux%u MPEG-1 YUV420 scalar %s\n", frame.width, frame.height, mpeg_backend_name());
            ++frames;
        } else if (result == MPEG_NEED_INPUT) {
            size_t space = mpeg_input_space();
            size_t count = space < read_size ? space : read_size;
            if (!count) { result = MPEG_CAPACITY; failed = 1; break; }
            size_t bytes = fread(input, 1, count, file);
            if (ferror(file)) { failed = 1; break; }
            start = now();
            result = mpeg_feed(input, bytes, bytes == 0);
            feed_ns += elapsed(start);
            if (result < 0) failed = 1;
        } else {
            failed = 1;
        }
    }
#ifdef __xcore__
    hwtimer_free(timer);
#endif
    mpeg_memory_stats_t memory = mpeg_memory_stats();
    if (!memory.guards_ok || !frames) failed = 1;
    mpeg_close();
    if (mpeg_memory_stats().live) failed = 1;
    if (fclose(file)) failed = 1;
    if (output && fclose(output)) failed = 1;
    printf("Result: frames=%u hash=%08x decode_ns=%llu worst_next_us=%llu arena_peak=%lu arena_capacity=%lu status=%s code=%d\n",
           frames, (unsigned)hash, (unsigned long long)decode_ns,
           (unsigned long long)(worst_next_ns / 1000), (unsigned long)memory.peak,
           (unsigned long)memory.capacity, failed ? "FAIL" : "OK", result);
    if (decode_ns)
        printf("Decode-only rate: %llu.%02llu FPS (excludes input, output, hashing and feed copies)\n",
               (unsigned long long)((uint64_t)frames * UINT64_C(1000000000) / decode_ns),
               (unsigned long long)(((uint64_t)frames * UINT64_C(100000000000) / decode_ns) % 100));
    printf("Matched: codec_ns=%llu feed_ns=%llu read_bytes=%u\n",
           (unsigned long long)(decode_ns + feed_ns), (unsigned long long)feed_ns, read_size);
    return failed ? 1 : 0;
}
