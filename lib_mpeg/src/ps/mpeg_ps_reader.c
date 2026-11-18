#include <string.h>
#include "mpeg_ps_reader.h"

static int fill(mpeg_ps_reader_t *r)
{
    if (r->position < r->length) return 1;
    if (r->ended || r->error) return 0;
    r->position = r->length = 0;
    if (r->read(r->context, r->cache, sizeof(r->cache), &r->length) ||
        r->length > sizeof(r->cache)) {
        r->error = 1;
        return 0;
    }
    if (!r->length) r->ended = 1;
    return r->length != 0;
}

static int byte(mpeg_ps_reader_t *r)
{
    return fill(r) ? r->cache[r->position++] : -1;
}

static int skip(mpeg_ps_reader_t *r, unsigned bytes)
{
    while (bytes) {
        if (!fill(r)) { r->error = 1; return -1; }
        unsigned count = r->length - r->position;
        if (count > bytes) count = bytes;
        r->position += count;
        bytes -= count;
    }
    return 0;
}

static int packet_byte(mpeg_ps_reader_t *r)
{
    if (!r->remaining) { r->error = 1; return -1; }
    --r->remaining;
    int value = byte(r);
    if (value < 0) r->error = 1;
    return value;
}

static int next_packet(mpeg_ps_reader_t *r)
{
    if (r->ended) return r->error ? -1 : 0;
    for (;;) {
        int a = byte(r);
        if (a < 0) return r->error ? -1 : 0;
        if (a != 0 || byte(r) != 0 || byte(r) != 1) break;
        int code = byte(r);
        if (code < 0) break;
        if (!r->started && code != 0xba) break;
        r->started = 1;
        if (code == 0xb9) { r->ended = 1; return 0; }
        if (code == 0xba) {
            int marker = byte(r);
            if (marker < 0 || (marker >> 4) != 2 || skip(r, 7)) break;
            continue;
        }
        if (code < 0xbb) break;
        int high = byte(r), low = byte(r);
        if (high < 0 || low < 0) break;
        r->remaining = ((unsigned)high << 8) | (unsigned)low;
        if (code != 0xe0 && code != 0xc0) {
            if (skip(r, r->remaining)) break;
            r->remaining = 0;
            continue;
        }
        int marker = packet_byte(r);
        while (marker == 0xff) marker = packet_byte(r);
        if (marker >= 0 && (marker & 0xc0) == 0x40) {
            if (packet_byte(r) < 0) break;
            marker = packet_byte(r);
        }
        unsigned header = marker >= 0 && (marker >> 4) == 2 ? 4 :
                          marker >= 0 && (marker >> 4) == 3 ? 9 : 0;
        if (marker < 0 || (!header && marker != 0x0f) || header > r->remaining) break;
        if (header) {
            /* A 33-bit presentation time in 90 kHz units: three bits in the
             * marker, then 15 and 15, each followed by a marker bit. Any
             * decoding time that follows is not needed. */
            int b[4];
            for (unsigned i = 0; i < 4; ++i) b[i] = byte(r);
            if (b[3] < 0) break;
            unsigned long long pts = ((unsigned long long)((marker >> 1) & 7) << 30) |
                ((unsigned)b[0] << 22) | ((unsigned)(b[1] >> 1) << 15) |
                ((unsigned)b[2] << 7) | (unsigned)(b[3] >> 1);
            unsigned ms = (unsigned)(pts / 90);
            if (code == 0xc0) {
                if (!r->have_audio) r->audio_start_ms = ms;
                r->audio_ms = ms;
                r->have_audio = 1;
            } else if (!r->have_video_start) {
                r->video_start_ms = ms;
                r->have_video_start = 1;
            }
            if (skip(r, header - 4)) break;
        }
        r->remaining -= header;
        r->stream = (unsigned)code;
        if (!r->remaining) continue;
        return 1;
    }
    r->error = 1;
    return -1;
}

void mpeg_ps_init(mpeg_ps_reader_t *r, MPEG_PS_READ_CALLBACK mpeg_ps_read_fn read, void *context)
{
    memset(r, 0, sizeof(*r));
    r->read = read;
    r->context = context;
}

int mpeg_ps_read_video(mpeg_ps_reader_t *r, unsigned char *output,
                       unsigned capacity, unsigned *bytes,
                       MPEG_PS_AUDIO_CALLBACK mpeg_ps_audio_fn audio, void *audio_context)
{
    *bytes = 0;
    if (!capacity || r->error) return -1;
    while (*bytes < capacity) {
        if (!r->remaining) {
            int result = next_packet(r);
            if (result < 0) return -1;
            if (!result) return *bytes ? 1 : 0;
        }
        if (!fill(r)) { r->error = 1; return -1; }
        unsigned count = r->length - r->position;
        if (count > r->remaining) count = r->remaining;
        if (r->stream == 0xe0) {
            if (count > capacity - *bytes) count = capacity - *bytes;
            memcpy(output + *bytes, r->cache + r->position, count);
            *bytes += count;
            r->video_bytes += count;
        } else {
            if (audio(audio_context, r->cache + r->position, count)) {
                r->error = 1;
                return -1;
            }
            r->audio_bytes += count;
        }
        r->position += count;
        r->remaining -= count;
    }
    return 1;
}

int mpeg_ps_audio_position(const mpeg_ps_reader_t *r, unsigned *ms)
{
    if (!r->have_audio || !r->have_video_start) return 0;
    *ms = r->audio_ms > r->video_start_ms ? r->audio_ms - r->video_start_ms : 0;
    return 1;
}

int mpeg_ps_audio_start(const mpeg_ps_reader_t *r, int *ms)
{
    if (!r->have_audio || !r->have_video_start) return 0;
    *ms = (int)r->audio_start_ms - (int)r->video_start_ms;
    return 1;
}
