// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Streaming planar 4:2:0 comparison; at most two frames resident on the host. */
int main(int argc, char **argv)
{
    if (argc != 5) return 2;
    unsigned width = (unsigned)strtoul(argv[3], NULL, 10);
    unsigned height = (unsigned)strtoul(argv[4], NULL, 10);
    if (!width || !height || width > 4096 || height > 4096 || (width | height) & 1) return 2;
    size_t sizes[3] = {width * height, width * height / 4, width * height / 4};
    size_t frame_bytes = sizes[0] + sizes[1] + sizes[2];
    unsigned char *a = malloc(frame_bytes), *b = malloc(frame_bytes);
    FILE *reference = fopen(argv[1], "rb"), *actual = fopen(argv[2], "rb");
    if (!a || !b || !reference || !actual) return 2;
    uint64_t sse[3] = {0}, samples[3] = {0}, changes[3] = {0};
    int64_t bias[3] = {0};
    unsigned peak[3] = {0}, frames = 0, worst_frame[3] = {0};
    double worst_mse[3] = {0}, first_sse[3] = {0}, last_sse[3] = {0};
    uint64_t recent[100][3] = {{0}};
    for (;;) {
        size_t na = fread(a, 1, frame_bytes, reference);
        size_t nb = fread(b, 1, frame_bytes, actual);
        if (!na && !nb) break;
        if (na != frame_bytes || nb != frame_bytes) {
            fprintf(stderr, "YUV length mismatch/truncated frame at %u\n", frames);
            return 1;
        }
        size_t offset = 0;
        for (unsigned p = 0; p < 3; ++p) {
            uint64_t frame_sse = 0;
            for (size_t i = 0; i < sizes[p]; ++i) {
                int delta = (int)b[offset + i] - a[offset + i];
                unsigned magnitude = (unsigned)abs(delta);
                frame_sse += (unsigned)(delta * delta);
                bias[p] += delta;
                changes[p] += delta != 0;
                if (magnitude > peak[p]) peak[p] = magnitude;
            }
            double mse = (double)frame_sse / sizes[p];
            if (mse > worst_mse[p]) {
                worst_mse[p] = mse;
                worst_frame[p] = frames;
            }
            if (frames < 100) first_sse[p] += frame_sse;
            last_sse[p] += (double)frame_sse - recent[frames % 100][p];
            recent[frames % 100][p] = frame_sse;
            sse[p] += frame_sse;
            samples[p] += sizes[p];
            offset += sizes[p];
        }
        ++frames;
    }
    if (ferror(reference) || ferror(actual) || !frames) return 1;
    printf("frames=%u size=%ux%u\n", frames, width, height);
    for (unsigned p = 0; p < 3; ++p) {
        double mse = (double)sse[p] / samples[p];
        printf("%s peak=%u mse=%.6f psnr=%.3f bias=%.6f changed=%.3f%% worst-frame=%u worst-mse=%.6f first100-mse=%.6f last100-mse=%.6f\n",
               p == 0 ? "Y" : p == 1 ? "Cb" : "Cr", peak[p], mse,
               mse ? 10 * log10(65025 / mse) : INFINITY,
               (double)bias[p] / samples[p], 100.0 * changes[p] / samples[p],
               worst_frame[p], worst_mse[p],
               first_sse[p] / ((frames < 100 ? frames : 100) * sizes[p]),
               last_sse[p] / ((frames < 100 ? frames : 100) * sizes[p]));
    }
    free(a); free(b);
    return fclose(reference) || fclose(actual);
}
