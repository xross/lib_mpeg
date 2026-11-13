lib_mpeg change log
===================

UNRELEASED
----------

  * CHANGED: Scalar MPEG-1 motion compensation evaluates each average input once;
    xcore reads aligned destination words directly, with unchanged interpolation
    rounding
  * ADDED: Native and standalone xcore motion-compensation tests against an
    independent per-pixel reference for both block sizes and all alignments

0.1.0
-----

  * Initial release
