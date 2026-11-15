/* Standalone fork API, without lib_mpeg, in the alternative channel layout. */
#include <stdio.h>
#define PLM_AUDIO_SEPARATE_CHANNELS
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    FILE *output = fopen(argv[2], "wb");
    if (!output) return 2;
    for (unsigned play = 0; play < 2; ++play) {
        plm_buffer_t *input = plm_buffer_create_with_filename(argv[1]);
        if (!input) return 2;
        plm_audio_t *decoder = plm_audio_create_with_buffer(input, 1);
        unsigned blocks = 0;
        plm_samples_t *samples;
        while ((samples = plm_audio_decode(decoder))) {
            plm_audio_sample_t interleaved[PLM_AUDIO_SAMPLES_PER_FRAME * 2];
            for (unsigned i = 0; i < samples->count; ++i) {
                interleaved[2*i] = samples->left[i];
                interleaved[2*i+1] = samples->right[i];
            }
            if (fwrite(interleaved, sizeof(*interleaved), samples->count * 2, output) != samples->count * 2)
                return 1;
            ++blocks;
        }
        if (!blocks) return 1;
        plm_audio_destroy(decoder);
    }
    return fclose(output);
}
