Upstream dependency and local adaptation
========================================

Default: PL_MPEG
----------------

* Project/repository: https://github.com/phoboslab/pl_mpeg.
* Location: ``third_party/pl_mpeg`` (Git submodule).
* Pinned commit: ``c871f2be022ece7ef4f64230b4fb8e1fb9eb6023`` (2025-12-30).
* License: MIT, declared in the header and upstream README.
* Original attribution: Dominic Szablewski, https://phoboslab.org.

The checkout is pristine and the header is included directly; no generated-source
transformation or fork is used. ``lib_mpeg/src/pl_mpeg/mpeg_video.c`` defines
upstream's documented allocation, buffer-size and no-stdio macros. Decoder
allocations are routed to a guarded static arena, not a heap. The adapter
preflights initial dimensions and MPEG-2 extensions, bounds compressed input,
and drains the delayed anchor that upstream misses after a final coded B picture.
That drain uses private state at this exact pin; native/FFmpeg tests cover it.
No VPU, audio, demux or timestamp-arithmetic changes have been made to upstream.

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
