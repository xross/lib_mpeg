#ifndef APP_PLMPEG_BENCHMARK_CONF_H_
#define APP_PLMPEG_BENCHMARK_CONF_H_

/* Configure the library here rather than editing its sources or CMake flags.
 * These match the library defaults. Use MPEG_VIDEO_IDCT_PLMPEG for upstream's
 * original, bit-exact transform. Native runs use the vector arithmetic model. */
#ifndef MPEG_VIDEO_IDCT
#ifdef __XS3A__
#define MPEG_VIDEO_IDCT MPEG_VIDEO_IDCT_VPU16
#else
#define MPEG_VIDEO_IDCT MPEG_VIDEO_IDCT_SCALAR16
#endif
#endif

#endif
