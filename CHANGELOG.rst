lib_mpeg change log
===================

UNRELEASED
----------

  * CHANGED: One automatic video arena layout reserves decoder/picture storage
    and guards, then uses the remainder for compressed input; initial-header
    staging avoids decoder recreation, buffer preferences and silent fallback
  * ADDED: Actual compressed-input capacity reporting for applications and benchmarks
  * FIXED: Preserve final buffered video frames when end-of-stream is signalled
    before input-buffer compaction
  * CHANGED: PL_MPEG buffer compaction avoids short shifts and uses non-overlapping
    memcpy chunks for larger discards
  * CHANGED: Hardware tests use test_support's Pyxrun.run() for execution, deadlines
    and cleanup, retaining per-attempt logs; dependencies are in tests/requirements.txt
  * ADDED: Optional saturating S32 MP2 output with a standalone scalar synthesis
    model, XS3 VPU window, generated fixed-point bounds and four-way target benchmark
  * ADDED: S32 PCM accuracy, saturation, scalar/vector identity and replay tests;
    float remains the default and the audio arena remains 28 KiB
  * ADDED: Exact nominal sequence-header frame rate in decoded video frames
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
