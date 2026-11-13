/* Copyright 2026 XMOS LIMITED.
 * SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef __xcore__
#include <xcore/hwtimer.h>
typedef uint32_t bench_time_t;
static bench_time_t now(void) { return get_reference_time(); }
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
#include "mpeg2.h"
#include "arena.h"
#include "mpeg2_benchmark.h"

enum { INPUT_BYTES = 4096 };
static uint8_t input[INPUT_BYTES];
static const uint8_t sequence_end[] = {0, 0, 1, 0xb7};

static int supported(const mpeg2_sequence_t *s)
{
#ifdef MPEG_BENCH_ALLOW_MPEG1
    int format = !(s->flags & SEQ_FLAG_MPEG2) || (s->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE);
#else
    int format = (s->flags & SEQ_FLAG_MPEG2) && (s->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE);
#endif
    return format &&
        s->picture_width && s->picture_height && s->width <= 320 && s->height <= 240 &&
        s->chroma_width * 2 == s->width && s->chroma_height * 2 == s->height;
}

static int output_picture(FILE *output, const mpeg2_info_t *info, uint32_t *hash)
{
    const mpeg2_sequence_t *s = info->sequence;
    for (unsigned plane = 0; plane < 3; ++plane) {
        unsigned width = plane ? (s->picture_width + 1) / 2 : s->picture_width;
        unsigned height = plane ? (s->picture_height + 1) / 2 : s->picture_height;
        unsigned stride = plane ? s->chroma_width : s->width;
        const uint8_t *row = info->display_fbuf->buf[plane];
        for (unsigned y = 0; y < height; ++y, row += stride) {
            for (unsigned x = 0; x < width; ++x) *hash = (*hash ^ row[x]) * UINT32_C(16777619);
            if (output && fwrite(row, 1, width, output) != width) return 0;
        }
    }
    return 1;
}

int mpeg2_benchmark(const char *input_path, const char *output_path, unsigned read_size)
{
    if (!read_size || read_size > INPUT_BYTES) return 1;
    FILE *file = fopen(input_path, "rb");
    if (!file) { fprintf(stderr, "Cannot open %s\n", input_path); return 1; }
    FILE *output = output_path ? fopen(output_path, "wb") : NULL;
    if (output_path && !output) { fclose(file); return 1; }
    benchmark_arena_install();
    mpeg2_accel(0);
    mpeg2dec_t *decoder = mpeg2_init();
    if (!decoder) { fclose(file); if (output) fclose(output); return 1; }
    const mpeg2_info_t *info = mpeg2_info(decoder);
    unsigned frames = 0, counts[4] = {0}, active_type = 0;
    unsigned eof = 0, saw_sequence = 0, failed = 0;
    uint64_t parse_ns = 0, feed_ns = 0, picture_ns = 0, type_ns[4] = {0}, worst_ns[4] = {0};
    uint32_t hash = UINT32_C(2166136261);
    while (!failed) {
        bench_time_t start = now();
        mpeg2_state_t state = mpeg2_parse(decoder);
        uint64_t duration = elapsed(start);
        parse_ns += duration;
        if (active_type) picture_ns += duration;
        switch (state) {
        case STATE_BUFFER: {
            if (eof) goto finished;
            size_t bytes = fread(input, 1, read_size, file);
            if (ferror(file)) { failed = 1; break; }
            if (!bytes) {
                /* Flush delayed reference pictures when a valid elementary
                 * stream omits its optional final sequence-end code. */
                memcpy(input, sequence_end, sizeof(sequence_end));
                bytes = sizeof(sequence_end);
                eof = 1;
            }
            start = now();
            mpeg2_buffer(decoder, input, input + bytes);
            feed_ns += elapsed(start);
            break;
        }
        case STATE_SEQUENCE:
        case STATE_SEQUENCE_REPEATED:
        case STATE_SEQUENCE_MODIFIED:
            if (!supported(info->sequence)) {
                fprintf(stderr, "Unsupported stream: require progressive MPEG-2 YUV420 up to 320x240\n");
                failed = 1;
                break;
            }
            if (!saw_sequence) {
                printf("Sequence: %ux%u coded=%ux%u frame_period=%u (27 MHz)\n",
                       info->sequence->picture_width, info->sequence->picture_height,
                       info->sequence->width, info->sequence->height, info->sequence->frame_period);
                saw_sequence = 1;
            }
            break;
        case STATE_PICTURE:
            active_type = info->current_picture->flags & PIC_MASK_CODING_TYPE;
            picture_ns = 0;
            if (active_type < 1 || active_type > 3) failed = 1;
            break;
        case STATE_SLICE:
            if (active_type) {
                ++counts[active_type];
                type_ns[active_type] += picture_ns;
                if (picture_ns > worst_ns[active_type]) worst_ns[active_type] = picture_ns;
                active_type = 0;
            }
            /* fall through */
        case STATE_END:
            if (info->display_fbuf) {
                if (!output_picture(output, info, &hash)) failed = 1;
                ++frames;
            }
            break;
        case STATE_INVALID:
        case STATE_INVALID_END:
        case STATE_PICTURE_2ND:
        case STATE_SLICE_1ST:
            fprintf(stderr, "Invalid/unsupported decoder state=%d\n", state);
            failed = 1;
            break;
        default:
            break;
        }
    }
finished:
    if (!benchmark_arena_guards_ok()) failed = 1;
    size_t peak = benchmark_arena_peak();
    mpeg2_close(decoder);
    if (benchmark_arena_live()) failed = 1;
    if (fclose(file)) failed = 1;
    if (output && fclose(output)) failed = 1;
    if (!saw_sequence || !frames) failed = 1;
    printf("Result: frames=%u hash=%08x parse_ns=%llu arena_peak=%lu arena_capacity=%u status=%s\n",
           frames, (unsigned)hash, (unsigned long long)parse_ns, (unsigned long)peak,
           MPEG2_ARENA_BYTES, failed ? "FAIL" : "OK");
    for (unsigned type = 1; type <= 3; ++type)
        printf("%c pictures=%u mean_us=%llu worst_us=%llu\n", "?IPB"[type], counts[type],
               (unsigned long long)(counts[type] ? type_ns[type] / counts[type] / 1000 : 0),
               (unsigned long long)(worst_ns[type] / 1000));
    if (parse_ns)
        printf("Decode-only rate: %llu.%02llu FPS (excludes input, output and hashing)\n",
               (unsigned long long)((uint64_t)frames * UINT64_C(1000000000) / parse_ns),
               (unsigned long long)(((uint64_t)frames * UINT64_C(100000000000) / parse_ns) % 100));
    printf("Matched: codec_ns=%llu feed_ns=%llu read_bytes=%u\n",
           (unsigned long long)(parse_ns + feed_ns), (unsigned long long)feed_ns, read_size);
    return failed ? 1 : 0;
}
