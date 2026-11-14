lib_mpeg change log
===================

UNRELEASED
----------

  * CHANGED: Motion compensation shares specialised prediction/blending loops
    and sliding-word reuse across platforms; xcore supplies aligned access and
    LEXTRACT, with unchanged pixel rounding
  * ADDED: Plane-edge, unaligned-buffer, padding-free portable and big-endian
    word-model motion tests
  * ADDED: XS3 VPU inverse DCT with a bit-identical scalar model and configurable
    original transform
  * ADDED: MP2 push-input decoding, bounded application-supplied audio memory and
    self-contained elementary-stream PCM precision tests
  * CHANGED: Video decoder memory is supplied by the application; video and audio
    have separate guarded arenas
  * CHANGED: Decoder arithmetic is configured through mpeg_conf.h; native tests
    share one build project and USB/LCD hardware tools share one report runner
  * ADDED: Bit-exact XS3 VPU YUV420 to RGB565 conversion with small stack scratch,
    no lookup tables and a scalar build option
  * ADDED: Exhaustive native VPU arithmetic-model and hardware colour tests,
    including alignment, SIMD tails and conversion-only timing
  * CHANGED: Scalar MPEG-1 motion compensation evaluates each average input once;
    xcore reads aligned destination words directly, with unchanged interpolation
    rounding
  * ADDED: Native and standalone xcore motion-compensation tests against an
    independent per-pixel reference for both block sizes and all alignments

0.1.0
-----

  * Initial release
