lib_mpeg change log
===================

UNRELEASED
----------

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
