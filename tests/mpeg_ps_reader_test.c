#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mpeg_ps_reader.h"

static const unsigned char fixture[] = {
    0,0,1,0xba, 0x21,0,1,0,1,0,0,1,
    0,0,1,0xe0, 0,5, 0x0f, 1,2,3,4,
    0,0,1,0xc0, 0,5, 0x0f, 11,12,13,14,
    0,0,1,0xe0, 0,3, 0x0f, 5,6,
    0,0,1,0xb9
};
static unsigned char input[sizeof(fixture)], video[32], audio[32];
static unsigned position, size, fragment, audio_size, fail_read, fail_audio;

MPEG_PS_READ_CALLBACK static int read_data(void *context, unsigned char *data, unsigned capacity, unsigned *bytes)
{
    (void)context;
    *bytes = size - position;
    if (*bytes > fragment) *bytes = fragment;
    if (*bytes > capacity) *bytes = capacity;
    memcpy(data, input + position, *bytes);
    position += *bytes;
    return fail_read ? -1 : 0;
}
MPEG_PS_AUDIO_CALLBACK static int audio_data(void *context, const unsigned char *data, unsigned bytes)
{
    (void)context;
    assert(audio_size + bytes <= sizeof(audio));
    memcpy(audio + audio_size, data, bytes);
    audio_size += bytes;
    return fail_audio ? -1 : 0;
}
static int play(unsigned capacity)
{
    mpeg_ps_reader_t reader;
    mpeg_ps_init(&reader, read_data, NULL);
    unsigned total = 0, bytes;
    position = audio_size = 0;
    int result;
    while ((result = mpeg_ps_read_video(&reader, video + total, capacity, &bytes, audio_data, NULL)) > 0) {
        total += bytes;
        assert(total <= sizeof(video));
    }
    if (result == 0) {
        assert(total == 6 && audio_size == 4);
        for (unsigned i = 0; i < total; ++i) assert(video[i] == i + 1);
        for (unsigned i = 0; i < audio_size; ++i) assert(audio[i] == i + 11);
    }
    return result;
}
/* Presentation times. Video starts at 0.54 s (48600 ticks of 90 kHz). Audio
 * packets are at 0.54 s and then, with a decoding time as well, 1.5 s (135000).
 * The audio position is the newest audio time less the first video time. */
static const unsigned char timed[] = {
    0,0,1,0xba, 0x21,0,1,0,1,0,0,1,
    0,0,1,0xc0, 0,3, 0x0f, 21,22,                              /* audio, no time */
    0,0,1,0xe0, 0,7, 0x21,0x00,0x03,0x7b,0xb1, 1,2,
    0,0,1,0xc0, 0,7, 0x21,0x00,0x03,0x7b,0xb1, 11,12,
    0,0,1,0xe0, 0,7, 0x21,0x00,0x05,0xbf,0x21, 3,4,            /* later video */
    0,0,1,0xc0, 0,12, 0x31,0x00,0x09,0x1e,0xb1, 0x11,0x00,0x09,0x1e,0xb1, 13,14,
    0,0,1,0xe0, 0,3, 0x0f, 5,6,
    0,0,1,0xb9
};
static unsigned timed_position, timed_fragment;
MPEG_PS_READ_CALLBACK static int read_timed(void *context, unsigned char *data, unsigned capacity, unsigned *bytes)
{
    (void)context;
    *bytes = sizeof(timed) - timed_position;
    if (*bytes > timed_fragment) *bytes = timed_fragment;
    if (*bytes > capacity) *bytes = capacity;
    memcpy(data, timed + timed_position, *bytes);
    timed_position += *bytes;
    return 0;
}
static void timestamps(void)
{
    for (timed_fragment = 1; timed_fragment <= sizeof(timed); ++timed_fragment) {
        mpeg_ps_reader_t reader;
        unsigned ms = 99, bytes, total = 0, seen_zero = 0;
        int result;
        timed_position = audio_size = 0;
        mpeg_ps_init(&reader, read_timed, NULL);
        assert(!mpeg_ps_audio_position(&reader, &ms) && ms == 99);
        /* One video byte at a time, so the position is seen as it advances. */
        while ((result = mpeg_ps_read_video(&reader, video + total, 1, &bytes, audio_data, NULL)) > 0) {
            total += bytes;
            if (mpeg_ps_audio_position(&reader, &ms)) {
                assert(ms == 0 || ms == 960);
                seen_zero |= ms == 0;
            } else {
                assert(ms == 99 || ms == 0);
            }
        }
        assert(result == 0 && total == 6 && audio_size == 6 && seen_zero);
        assert(mpeg_ps_audio_position(&reader, &ms) && ms == 960);
        int start = 99;
        assert(mpeg_ps_audio_start(&reader, &start) && start == 0);
        for (unsigned i = 0; i < total; ++i) assert(video[i] == i + 1);
    }
}

int main(void)
{
    for (fragment = 1; fragment <= sizeof(fixture); ++fragment) {
        for (unsigned capacity = 1; capacity <= 8; ++capacity) {
            memcpy(input, fixture, sizeof(input)); size = sizeof(input);
            assert(play(capacity) == 0);
        }
    }
    fragment = 7;
    for (unsigned cut = 13; cut < 23; ++cut) {
        memcpy(input, fixture, sizeof(input)); size = cut;
        assert(play(4) == -1);
    }
    memcpy(input, fixture, sizeof(input)); size = sizeof(input); input[4] = 0x40;
    assert(play(4) == -1); /* Do not misparse an MPEG-2 pack header. */
    memcpy(input, fixture, sizeof(input)); input[18] = 0xff;
    assert(play(4) == -1); /* Stuffing cannot underflow a PES payload length. */
    memcpy(input, fixture, sizeof(input)); fail_read = 1;
    assert(play(4) == -1);
    fail_read = 0; fail_audio = 1;
    assert(play(4) == -1);
    fail_audio = 0;
    timestamps();
    puts("PASS: PS cache/PES boundaries, timestamps, replay, truncation, malformed headers and I/O/sink errors");
    return 0;
}
