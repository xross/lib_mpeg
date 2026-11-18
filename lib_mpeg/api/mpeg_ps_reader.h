#ifndef LIB_MPEG_PS_READER_H_
#define LIB_MPEG_PS_READER_H_

#include "mpeg_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __xcore__
#define MPEG_PS_CALLBACK(name) __attribute__((fptrgroup("mpeg_ps_" #name)))
#else
#define MPEG_PS_CALLBACK(name)
#endif
#define MPEG_PS_READ_CALLBACK MPEG_PS_CALLBACK(read)
#define MPEG_PS_AUDIO_CALLBACK MPEG_PS_CALLBACK(audio)
typedef int (*mpeg_ps_read_fn)(void *, unsigned char *, unsigned, unsigned *);
typedef int (*mpeg_ps_audio_fn)(void *, const unsigned char *, unsigned);

typedef struct {
    unsigned char cache[MPEG_PS_CACHE_BYTES] __attribute__((aligned(4)));
    unsigned position, length, remaining, stream, ended, error, started;
    unsigned video_bytes, audio_bytes;
    /* Presentation times in ms: the first video packet's, and the first and
     * newest audio packets'. Valid once such a packet with a timestamp was read. */
    unsigned video_start_ms, audio_start_ms, audio_ms, have_video_start, have_audio;
    MPEG_PS_READ_CALLBACK mpeg_ps_read_fn read;
    void *context;
} mpeg_ps_reader_t;

/* Sequential MPEG-1 PS only: first video (E0) and MP2 audio (C0). Other PES
 * streams are skipped. Packet payloads stream through one cache, with no
 * packet-sized allocations. Audio callbacks consume their span before return.
 * read callback: 0 on success (zero bytes = EOF), nonzero on I/O failure.
 * read_video: 1 = video bytes, 0 = clean EOF, -1 = read/format/truncation error. */
void mpeg_ps_init(mpeg_ps_reader_t *reader, MPEG_PS_READ_CALLBACK mpeg_ps_read_fn read, void *context);
int mpeg_ps_read_video(mpeg_ps_reader_t *reader, unsigned char *output,
                       unsigned capacity, unsigned *bytes,
                       MPEG_PS_AUDIO_CALLBACK mpeg_ps_audio_fn audio, void *audio_context);
/* How far the audio handed to the callback reaches, in ms after the start of
 * the video: the presentation time of the newest audio packet, less that of the
 * first video packet. Returns 0 until both are known. A program stream carries
 * audio well after the video for the same moment, so this is normally behind
 * the video already returned. */
int mpeg_ps_audio_position(const mpeg_ps_reader_t *reader, unsigned *ms);
/* When the audio starts, in ms after the start of the video; negative if it
 * starts first. Returns 0 until both are known. */
int mpeg_ps_audio_start(const mpeg_ps_reader_t *reader, int *ms);
#ifdef __cplusplus
}
#endif
#endif
