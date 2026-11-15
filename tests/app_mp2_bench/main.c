#include <stdint.h>
#include <stdio.h>
#include <xcore/hwtimer.h>
#include <xscope.h>
#include "mpeg_audio.h"

/* Input refill, checksums and xSCOPE PCM output are never timed.
 * The runner embeds the compressed fixture before building. Its RAM
 * is benchmark staging only, not part of the decoder's 28-KiB arena. */
#include "audio_bench_input.h"
static unsigned char memory[MPEG_AUDIO_MEMORY_SIZE] __attribute__((aligned(8)));

/* No callbacks installed by the bounded adapter; constrain PL_MPEG's generic
 * indirect callback graph, as in the player. Functional bounds checked below. */
#pragma stackfunction 1024
int main(void)
{
    /* Complete PCM, not lossy telemetry. Any transport stalls are confined to
     * the dump between calls, outside the decoder timing interval. */
    xscope_mode_lossless();
    const unsigned size = sizeof(input);
    int failed = 0;
    if (mpeg_audio_open(memory, sizeof(memory)) != MPEG_NEED_INPUT) return 1;
    unsigned offset = 0, blocks = 0, rate = 0, total = 0, least = UINT32_MAX, most = 0;
    unsigned empty_ticks = 0, empty_calls = 0, checksum = 0, full_checksum = 2166136261u;
    for (;;) {
        mpeg_audio_frame_t frame;
        unsigned start = get_reference_time();
        mpeg_result_t result = mpeg_audio_next(&frame);
        unsigned ticks = get_reference_time() - start;
        if (result == MPEG_FRAME) {
            total += ticks;
            if (ticks < least) least = ticks;
            if (ticks > most) most = ticks;
            ++blocks;
            rate = frame.sample_rate;
            const uint8_t *bytes = (const uint8_t *)frame.samples;
            const unsigned count = MPEG_AUDIO_FRAME_SAMPLES * MPEG_AUDIO_CHANNELS;
            uint32_t last;
            __builtin_memcpy(&last, frame.samples + count - 1, sizeof(last));
            checksum = checksum * 33 ^ last;
            for (unsigned i = 0; i < count * sizeof(*frame.samples); ++i)
                full_checksum = (full_checksum ^ bytes[i]) * 16777619u;
            /* Basic xSCOPE text I/O, no file descriptors or host syscalls.
             * Hex is bounded/framed so the runner verifies every PCM byte. */
            static const char hex[] = "0123456789abcdef";
            for (unsigned offset = 0; offset < count * sizeof(*frame.samples); offset += 128) {
                char line[257];
                for (unsigned i = 0; i < 128; ++i) {
                    unsigned value = bytes[offset + i];
                    line[2*i] = hex[value >> 4];
                    line[2*i+1] = hex[value & 15];
                }
                line[256] = 0;
                printf("PCM %s\n", line);
            }
        } else if (result == MPEG_NEED_INPUT) {
            empty_ticks += ticks;
            ++empty_calls;
            unsigned count = mpeg_audio_input_space();
            if (count > size - offset) count = size - offset;
            if (!count) {
                failed = offset != size || mpeg_audio_finish() != MPEG_END || !blocks;
                break;
            }
            if (mpeg_audio_feed(input + offset, count) != MPEG_NEED_INPUT) { failed = 1; break; }
            offset += count;
        } else { failed = 1; break; }
    }
    mpeg_memory_stats_t stats = mpeg_audio_memory_stats();
    if (!stats.guards_ok || stats.peak > MPEG_AUDIO_MEMORY_SIZE) failed = 1;
    mpeg_audio_close();
    if (mpeg_audio_memory_stats().live) failed = 1;
    if (failed) return 1;
    printf("PASS: blocks=%u rate=%u ticks=%u min=%u max=%u arena=%u checksum=%08x pcm=%08x empty_calls=%u empty_ticks=%u\n",
        blocks, rate, total, least, most, (unsigned)stats.peak, checksum, full_checksum, empty_calls, empty_ticks);
    return 0;
}
