#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "mpeg_ps_reader.h"

typedef struct {
    FILE *input;
    unsigned fragment;
} source_t;

MPEG_PS_READ_CALLBACK static int read_data(void *context, unsigned char *data, unsigned capacity, unsigned *bytes)
{
    source_t *source = context;
    if (capacity > source->fragment) capacity = source->fragment;
    *bytes = (unsigned)fread(data, 1, capacity, source->input);
    return ferror(source->input) ? -1 : 0;
}

MPEG_PS_AUDIO_CALLBACK static int write_audio(void *context, const unsigned char *data, unsigned bytes)
{
    return fwrite(data, 1, bytes, context) == bytes ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: %s input.mpg output.m1v output.mp2 read-fragment output-capacity\n", argv[0]);
        return 1;
    }
    unsigned fragment = (unsigned)strtoul(argv[4], NULL, 10);
    unsigned capacity = (unsigned)strtoul(argv[5], NULL, 10);
    unsigned char data[4096];
    assert(fragment && capacity && capacity <= sizeof(data));
    source_t source = {fopen(argv[1], "rb"), fragment};
    FILE *video = fopen(argv[2], "wb"), *audio = fopen(argv[3], "wb");
    assert(source.input && video && audio);
    mpeg_ps_reader_t reader;
    mpeg_ps_init(&reader, read_data, &source);
    unsigned bytes;
    int result;
    while ((result = mpeg_ps_read_video(&reader, data, capacity, &bytes, write_audio, audio)) > 0)
        assert(fwrite(data, 1, bytes, video) == bytes);
    assert(result == 0 && !reader.error && reader.video_bytes);
    assert(fclose(source.input) == 0);
    assert(fclose(video) == 0);
    assert(fclose(audio) == 0);
    return 0;
}
