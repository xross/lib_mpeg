Upstream dependency and local adaptation
========================================

Default: PL_MPEG
----------------

* Upstream project: https://github.com/phoboslab/pl_mpeg.
* License: MIT, declared in the header and upstream README.
* Original attribution: Dominic Szablewski, https://phoboslab.org.

Two checkouts are kept:

* ``third_party/pl_mpeg``: the fork https://github.com/xross/pl_mpeg, branch
  ``master``, based on upstream commit
  ``c871f2be022ece7ef4f64230b4fb8e1fb9eb6023`` (2025-12-30). This is what the
  library compiles.
* ``third_party/pl_mpeg_upstream``: https://github.com/phoboslab/pl_mpeg pinned
  at that same upstream commit and never edited. Only the host test program
  ``tests/plmpeg_upstream_ref.c`` is built from it, so the byte-exact output
  test compares the fork with the original, not with itself. It is not part
  of any xcore build.

The fork changes decoding speed only; its output is required to be identical to
upstream. The changes are all in ``pl_mpeg.h``. The first four are portable C,
one commit each, intended for submission upstream; the fifth is xcore-specific
and its commit subject is prefixed ``xcore:``, as any later xcore-only change
should be, so that the portable commits can be cherry-picked:

* DCT coefficients are decoded from a 32-bit window of the bitstream instead of
  one bounds-checked call per bit, falling back to the original reads within 48
  bits of the end of the buffered data.
* The IDCT skips columns and rows whose only non-zero coefficient is the first.
* Motion compensation processes four pixels per 32-bit word.
* Block output uses row pointers and skips constant blocks that add zero.
* On xcore only, motion compensation reads and writes aligned words directly,
  because unaligned word access traps. Frame memory must therefore be aligned
  to four bytes, which the adapter's arena guarantees.

The header is included directly; no generated-source transformation is used.
``lib_mpeg/src/pl_mpeg/mpeg_video.c`` defines upstream's documented allocation,
buffer-size and no-stdio macros. Decoder
allocations are routed to a guarded static arena, not a heap. The adapter
preflights initial dimensions and MPEG-2 extensions, bounds compressed input,
and drains the delayed anchor that upstream misses after a final coded B picture.
That drain uses private state at this exact pin; native/FFmpeg tests cover it.
No VPU, audio, demux or timestamp-arithmetic changes have been made.

Optional: libmpeg2
------------------

* Project: libmpeg2, https://libmpeg2.sourceforge.io/.
* Repository: https://code.videolan.org/videolan/libmpeg2.git.
* Location: ``third_party/libmpeg2`` (Git submodule).
* Pinned commit: ``946bf4b518aacc224f845e73708f99e394744499``.
* Upstream commit date: 2012-07-18.
* Header release identifier: 0.5.1. The pin is a development revision, **not**
  a byte-for-byte claim to be the 0.5.1 release archive.
* License: GPL-2.0-or-later. Original notices and complete license text are
  retained in the submodule, including ``COPYING`` and source-file headers.

The upstream checkout is pristine. ``cmake/upstream.cmake`` makes one explicit,
exact-match-checked source transformation to a build-directory copy of
``decode.c``: replace its fixed ``BUFFER_SIZE`` with ``MPEG2_CHUNK_BYTES``
(default 32768). The generated source preserves the upstream notices and adds
a dated downstream-change comment. Both native and xcore use that same change.
No downstream tag or upstream/fork publication has been made.

Configuration, allocation guards, benchmark code and build files remain outside
the third-party source tree. The build selects scalar sources only, supplies
``config.h``, and limits declared alignment to eight bytes on XTC; the benchmark
allocator still returns 64-byte-aligned addresses.

The internal XMOS third-party guidance has not been accessible in this environment;
full internal policy compliance and product licensing have not been verified.
