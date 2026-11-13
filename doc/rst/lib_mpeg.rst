#############################
lib_mpeg: MPEG video decoding
#############################

************
Introduction
************

``lib_mpeg`` is an experimental MPEG video decoder integration for XCORE devices.
It is a decoder bring-up and evaluation, not a complete media player.

Two backends are available. Only one is compiled into a build:

.. list-table:: Decoder backends
   :header-rows: 1

   * - ``MPEG_BACKEND``
     - Codec
     - API header
     - Upstream licence
   * - ``pl_mpeg`` (default)
     - MPEG-1 video
     - ``lib_mpeg/api/mpeg.h``
     - MIT
   * - ``libmpeg2``
     - MPEG-2 video
     - ``lib_mpeg/api/mpeg2.h``
     - GPL-2.0-or-later

Both are scalar C: there are no VPU kernels and no parallel decoding. The two
interfaces are not interchangeable. The default backend exports a small
push-input API; the optional backend retains the upstream libmpeg2 API. A shared
backend interface is deferred until real player requirements establish it.

The default build does not compile or link libmpeg2. The upstream sources and
what has been changed in them are described under `Third-party decoders`_;
licensing is described in ``LICENSE.rst``.

Not included
============

Upstream PL_MPEG contains program-stream demuxing and MP2 audio decoding, but the
adapter exposes and validates **video only**. It does not accept a multiplexed
``.mpg`` file and provides no audio samples. Demuxing, audio, display or storage
integration and playback pacing are left to the application.
No audio/video synchronisation claim is made.

***********************
The PL_MPEG adapter
***********************

PL_MPEG is a single-header desktop library: by default it allocates from the
heap, can read files through stdio, and grows its input buffer on demand. The
*adapter* is the XMOS-written layer that makes it usable on xcore and implements
the ``mpeg.h`` API. PL_MPEG itself is compiled from the ``third_party/pl_mpeg``
submodule, a fork whose only changes are speed optimisations that leave its
output identical to upstream; `Third-party decoders`_ lists them.

.. list-table:: Adapter source files (``lib_mpeg/src/pl_mpeg``)
   :header-rows: 1

   * - File
     - Purpose
   * - ``mpeg_video.c``
     - Configures and includes ``pl_mpeg.h``; implements ``mpeg_open()``,
       ``mpeg_feed()``, ``mpeg_next()`` and ``mpeg_close()``.
   * - ``mpeg_memory.c``, ``mpeg_memory.h``
     - Static arena allocator used in place of the heap; implements
       ``mpeg_memory_stats()``.

The adapter does the following.

Configures PL_MPEG
==================

PL_MPEG is customised only through the configuration macros it documents, defined
before the header is included:

* ``PLM_NO_STDIO`` removes file access; the application supplies all input.
* ``PLM_BUFFER_DEFAULT_SIZE`` is set to ``MPEG_INPUT_CAPACITY`` (32 KiB).
* ``PLM_MALLOC``, ``PLM_REALLOC`` and ``PLM_FREE`` are redirected to the arena
  allocator. Without these PL_MPEG would call ``malloc()``, ``realloc()`` and
  ``free()``.

Replaces the heap with a static arena
=====================================

Every PL_MPEG allocation is served from one static array of
``MPEG_ARENA_CAPACITY`` (384 KiB). The allocator functions
(``mpeg_memory_alloc()`` and related) are internal to the library; applications
do not provide or call them.

* Allocation is a simple bump allocator with 8-byte alignment and at most eight
  live blocks, which is sufficient for one decoder. Space is reclaimed only when
  every block has been freed, that is when the decoder is closed.
* Each block is surrounded by 16 guard bytes. They are verified on every free and
  reported by ``mpeg_memory_stats()``, which also gives peak and live usage.
* A resize request that would grow a block, an allocation that does not fit, a
  corrupted guard or an unknown pointer calls ``abort()``. PL_MPEG does not check
  allocation results, so failing fatally is safer than returning a null pointer
  into it. The stream checks below exist to keep supported input away from
  these paths.

Provides a push-input API
=========================

PL_MPEG's video decoder is wrapped as a single static instance driven by
``mpeg_feed()`` and ``mpeg_next()``. No callbacks are installed. Input is copied
into PL_MPEG's fixed-capacity buffer, and a feed larger than
``mpeg_input_space()`` is refused with ``MPEG_CAPACITY`` instead of growing the
buffer. Returned frames are views of PL_MPEG's own picture buffers, not copies.

Checks the stream before decoding
=================================

PL_MPEG sizes its picture allocations from the stream's sequence header, so the
adapter inspects the incoming bytes before PL_MPEG acts on them:

* The stream must begin with an MPEG-1 sequence header (start code
  ``00 00 01 B3``) with a non-zero size no larger than 320 x 240 and a valid
  frame-rate code.
* An extension start code (``00 00 01 B5``) before the first picture marks the
  stream as MPEG-2 and is rejected.
* Decoding does not start until the first picture start code has been fed.
* Each decoded picture must have the dimensions of the initial sequence header.

A failed check returns ``MPEG_UNSUPPORTED``. Like ``MPEG_ERROR``, it is latched:
every later call returns the same result until the decoder is closed. These
checks bound memory use for supported streams; they are not a general
malformed-stream validator.

Corrects end-of-stream behaviour
================================

Two upstream behaviours are corrected in the adapter, not by patching PL_MPEG:

* When the final coded picture is a B picture, PL_MPEG does not output the last
  anchor picture. The adapter detects this state at end of input and returns
  that picture once. See `Correctness`_.
* PL_MPEG does not register end of input when it is signalled with no bytes
  left in its buffer. The adapter sets the ended state directly, so a final
  ``mpeg_feed()`` call with ``size`` zero is valid.

Both rely on PL_MPEG internal state, so they must be re-verified whenever the
submodule pin is changed. A stream that ends without producing any picture
returns ``MPEG_ERROR``.

**********************
Third-party decoders
**********************

Both decoders are Git submodules under ``third_party``. The submodules record
the exact revisions in use.

PL_MPEG
=======

PL_MPEG is by Dominic Szablewski (https://phoboslab.org) and is MIT-licensed;
the upstream project is https://github.com/phoboslab/pl_mpeg. Two checkouts are
kept:

* ``third_party/pl_mpeg`` is the fork https://github.com/xross/pl_mpeg, based on
  upstream as of 2025-12-30. This is what the library compiles. The header is
  included directly, with no generated-source step.
* ``third_party/pl_mpeg_upstream`` is unmodified upstream at the revision the
  fork is based on. Only the host test program ``tests/plmpeg_upstream_ref.c``
  and the colour-conversion test are built from it, so the byte-exact output
  tests compare the library with the original and not with itself. It is not
  part of any xcore build.

The fork changes decoding speed only; its output must be identical to
upstream. The changes are all in ``pl_mpeg.h``:

* DCT coefficients are decoded from a 32-bit window of the bitstream instead of
  one bounds-checked call per bit, falling back to the original reads within 48
  bits of the end of the buffered data.
* The IDCT skips columns and rows whose only non-zero coefficient is the first.
* Motion compensation processes four pixels per 32-bit word. Its rounded-average
  helper evaluates each argument once, including load/extract expressions.
* Block output uses row pointers and skips constant blocks that add zero.
* On xcore only, motion compensation reads and writes aligned words directly,
  because unaligned word access traps. Frame memory must therefore be aligned
  to four bytes, which the adapter's arena guarantees. Bidirectional destination
  reads use the existing word alignment directly rather than the unaligned
  source-word loader.
  The original padded-word read contract is retained. There are no additional
  image buffers, tables or VPU kernels. Half-pixel rounding and the separate
  bidirectional rounding step remain unchanged.

The first four are portable C, intended for submission upstream. xcore-only
changes in the fork carry an ``xcore:`` prefix in their commit subject so that
the portable ones can be picked out. No audio, demux or timestamp-arithmetic
changes have been made.

libmpeg2
========

libmpeg2 (https://libmpeg2.sourceforge.io/) is licensed under GPL-2.0-or-later.
``third_party/libmpeg2`` is a checkout of
https://code.videolan.org/videolan/libmpeg2.git at a development revision dated
2012-07-18. Its header identifies release 0.5.1, but it is not claimed to be
byte-for-byte the 0.5.1 release archive. The original notices and the complete
licence text are in the submodule.

The checkout is unmodified. ``cmake/upstream.cmake`` makes one explicit,
exact-match-checked change to a build-directory copy of ``decode.c``: it
replaces the fixed ``BUFFER_SIZE`` with ``MPEG2_CHUNK_BYTES`` (default 32768).
The generated source keeps the upstream notices and adds a dated comment
recording the change. Native and xcore builds use the same change.

Configuration, allocation guards, benchmark code and build files stay outside
the third-party tree. The build selects scalar sources only, supplies
``config.h``, and limits declared alignment to eight bytes on XTC; the benchmark
allocator still returns 64-byte-aligned addresses.

*****
Usage
*****

``lib_mpeg`` is intended to be used with `XCommon CMake <https://www.xmos.com/file/xcommon-cmake-documentation/?version=latest>`_,
the XMOS application build and dependency management system.

To use the library in an application, add ``lib_mpeg`` to the application's
``APP_DEPENDENT_MODULES`` list in ``CMakeLists.txt``:

.. code-block:: cmake

    set(APP_DEPENDENT_MODULES "lib_mpeg(0.1.0)")

The upstream decoders are Git submodules, so the repository must be cloned with
``--recurse-submodules`` (or ``git submodule update --init`` run afterwards).

The default backend is accessed through ``mpeg.h``:

.. code-block:: c

    #include "mpeg.h"

To select the optional backend, set ``MPEG_BACKEND`` to ``libmpeg2`` before the
library is registered, or pass ``-DMPEG_BACKEND=libmpeg2`` when configuring.

***
API
***

The default (PL_MPEG) API is declared in ``lib_mpeg/api/mpeg.h``. The
application owns the decoder task and performs file or USB reads outside it:

#. Call ``mpeg_open()``; expect ``MPEG_NEED_INPUT``.
#. Call ``mpeg_next(&frame)``.
#. On ``MPEG_NEED_INPUT``, read at most ``mpeg_input_space()`` bytes and pass them
   to ``mpeg_feed(data, size, end_of_input)``. The input is copied, so the caller
   can reuse its buffer as soon as the call returns.
#. On ``MPEG_FRAME``, consume or copy the Y/Cb/Cr planes before the next
   ``mpeg_next()`` or ``mpeg_close()``. Use the per-plane byte strides; coded
   dimensions may be padded.
#. After declaring end of input, drain pictures until ``MPEG_END``, then call
   ``mpeg_close()``.
#. To replay or change streams, close and open a fresh decoder.

.. code-block:: c

    mpeg_frame_t frame;
    int eof = 0;

    mpeg_open();
    for (;;) {
        mpeg_result_t result = mpeg_next(&frame);
        if (result == MPEG_NEED_INPUT) {
            size_t space = mpeg_input_space();
            size_t n = read_input(buffer, space < sizeof(buffer) ? space : sizeof(buffer));
            eof = (n == 0);
            mpeg_feed(buffer, n, eof);
        } else if (result == MPEG_FRAME) {
            show_frame(&frame);
        } else {
            break;    /* MPEG_END or an error */
        }
    }
    mpeg_close();

Supported input
===============

Only a fixed-resolution, progressive MPEG-1 YUV420 elementary stream starting
with a sequence header is supported, up to ``MPEG_MAX_WIDTH`` x
``MPEG_MAX_HEIGHT`` (320 x 240). MPEG-2 extensions and oversized initial
dimensions are rejected with ``MPEG_UNSUPPORTED`` before picture allocation.

An oversized feed returns ``MPEG_CAPACITY`` and is not consumed. Parse and
format errors (``MPEG_ERROR``) are terminal; close before reopening. This is not
a complete malformed-stream validator, so only trusted input should be decoded.

Memory model
============

All decoder allocations come from a static arena of ``MPEG_ARENA_CAPACITY``
(384 KiB), including the three YUV420 pictures and the ``MPEG_INPUT_CAPACITY``
(32 KiB) compressed buffer. There is no heap fallback and no mid-stream buffer
growth. Unexpected upstream allocation requests abort instead of returning a
null pointer into upstream code, which does not consistently check for it.
``mpeg_memory_stats()`` reports the arena peak, live usage and guard status.

Only one decoder can be open per tile. Picture pointers are tile-local and
read-only; do not pass them to another tile as if they were shared memory.

Colour conversion to RGB565
===========================

``mpeg_rgb565.h`` converts decoded pictures for displays that take 16-bit
pixels. It is available with the PL_MPEG backend only.

.. code-block:: c

    #include "mpeg_rgb565.h"

    /* A whole frame; stride is the bytes from one output row to the next. */
    mpeg_frame_to_rgb565(&frame, framebuffer, 2 * frame.width);

    /* Or two rows at a time, sharing one row of chroma. */
    mpeg_yuv420_rows_to_rgb565(y0, y1, cb, cr, out0, out1, width);

Input is limited-range BT.601 YUV 4:2:0, as decoded. Each output pixel is two
bytes, most significant first (``RRRRRGGG GGGBBBBB``), the order SPI display
controllers such as the ST7789 expect. The result is identical to PL_MPEG's own
RGB conversion truncated to 5/6/5 bits; the test suite checks every Y/Cb/Cr
combination against unmodified upstream.

The two-row form exists so that a picture can be converted in strips without
holding a second full frame, and on a different tile from the decoder: it
shares no state with it. An odd width is rounded up to even, and output is
fastest when both output rows are 4-byte aligned. The conversion uses about
6 KB of lookup tables, built on the first call, on whichever tile calls it; make
that first call from a single task. It is scalar code, not VPU-vectorised. In
the ``lib_xuh`` ``app_xuh_msc`` example it takes about 13.4 ms for a 320 x 240
frame, including receiving the picture over a channel.

***************
Preparing input
***************

Convert a source to the video-only input format with FFmpeg::

    ffmpeg -i source.mp4 -t 10 -an \
      -vf "scale=320:240,setsar=1,fps=25" \
      -c:v mpeg1video -pix_fmt yuv420p -g 12 -bf 2 -q:v 6 \
      -f mpeg1video VIDEO.M1V

This stretches the complete picture to 320 x 240 without cropping. The encoded
frame rate is source timing, not a throughput promise. If a compressed picture
plus upstream lookahead exceeds the 32 KiB input bound, reduce quality or bitrate.

******************
Building and tests
******************

Requirements
============

* XMOS XTC Tools 15.3.1 with XCommon CMake, and CMake 3.21 or later.
* For native tests: a C99 compiler with sanitizer support, Python 3 and FFmpeg on
  the path.
* Optionally, ``requirements-dev.txt`` for checking the RST documents.

Native tests
============

From the repository root, the default PL_MPEG build and tests are::

    python3 -m venv .venv
    source .venv/bin/activate
    cmake -S . -B build-plmpeg-native -DCMAKE_BUILD_TYPE=Release
    cmake --build build-plmpeg-native -j 8
    ctest --test-dir build-plmpeg-native --output-on-failure

The suite decodes 24-picture I/P and I/P/B clips with input refills of 1, 7,
4093 and 4096 bytes, and covers repeated decode/reopen cycles, empty and invalid
inputs, dimension and codec rejection, and capacity limits. Output is compared
with a pristine upstream build and with FFmpeg; see `Correctness`_.

``mpeg_motion_test`` additionally checks the fork's actual macroblock function
against an independent per-pixel reference in 2128 cases. It covers 8- and
16-pixel blocks, every source alignment, positive and negative integer/half-pixel
motion, all interpolation modes and bidirectional averaging, using zero, 255,
alternating and deterministic random data. The entire destination is checked,
including bytes outside the written block. Build the same test for xcore to
exercise the aligned-load/LEXTRACT implementation rather than the native
memcpy path (XTC Tools active)::

    cmake -G "Unix Makefiles" -S tests/app_motion -B build-motion-xcore
    xmake -C build-motion-xcore -j 8
    xrun --io build-motion-xcore/bin/test_mpeg_motion.xe

This finite standalone test has no real-time USB/display tasks. JTAG output is
used only for the final pass/fail report, not a throughput measurement.

The optional MPEG-2 tests use a separate build directory::

    cmake -S . -B build-libmpeg2-native -DMPEG_BACKEND=libmpeg2
    cmake --build build-libmpeg2-native -j 8
    ctest --test-dir build-libmpeg2-native --output-on-failure

Native builds enable ASan and UBSan by default. They exclude the signed-shift
check and use ``-fwrapv`` for the upstream integer transforms; all other checks
are fatal. Add ``-DMPEG_SANITIZE=OFF`` in a separate build directory for
uninstrumented native timing. Native-host rates are not xcore performance.

XU316 examples
==============

``examples/app_plmpeg_benchmark`` and ``examples/app_mpeg2_benchmark`` are
standalone decode benchmarks for the ``XK-EVK-XU316`` target, one per backend.
With the XTC Tools activated in the same shell::

    cmake -G "Unix Makefiles" -S examples/app_plmpeg_benchmark -B build-plmpeg-xu316
    xmake -C build-plmpeg-xu316 -j 8

For MPEG-2 substitute ``app_mpeg2_benchmark`` and a different build directory.
Executables are written to the build directory's ``bin/`` so that comparison
builds cannot overwrite one another.

Each example runs a single decoder task on tile 1; tile 0 only runs startup and
runtime infrastructure. The benchmark reads ``VIDEO.M1V`` from the run working
directory using host file I/O over xSCOPE, which is always enabled. File I/O is
outside the timed decoder calls and is not a storage validation.

To run on hardware::

    xrun --xscope --xscope-io-only build-plmpeg-xu316/bin/app_plmpeg_benchmark.xe

For a small checked simulator smoke test, generate the deterministic clip and
run it through ``tests/run_xsim.py``::

    ffmpeg -hide_banner -loglevel error -y -f lavfi \
      -i testsrc2=size=320x240:rate=25 -frames:v 6 -an \
      -c:v mpeg1video -pix_fmt yuv420p -g 12 -bf 2 -q:v 6 \
      -f mpeg1video build-plmpeg-native/fixtures/VIDEO.M1V
    .venv/bin/python tests/run_xsim.py \
      --xe build-plmpeg-xu316/bin/app_plmpeg_benchmark.xe \
      --work build-plmpeg-native/fixtures --frames 6 --hash a24768f4

The runner bounds xsim to 600000000 cycles and a 180 s process timeout. Success
requires normal completion, the expected picture count and hash, and
``status=OK``. Reaching either bound is a failure, not a result.

Timing uses a hardware timer at the target's default 100 MHz reference clock,
accumulated into 64-bit nanoseconds outside the decoder call. Each timed call
must take less than the 42.9 s timer wrap period. An API call can decode ahead
before returning a picture, so the reported ``worst_next_us`` is a worst **API
call**, not a guaranteed single-picture cost.

Space optimisation
==================

``MPEG_SPACE_OPTIMIZE=ON`` is the default. File-specific compiler flags in
``lib_mpeg/lib_build_info.cmake`` keep the decoder kernels at ``-O3`` and build
cold files with ``-Os``:

* PL_MPEG: the single-header decoder translation unit stays at ``-O3``; the
  static allocator uses ``-Os``. Hot and cold functions inside that one
  translation unit are not tuned separately.
* libmpeg2: ``decode.c``, ``slice.c``, ``idct.c`` and ``motion_comp.c`` stay at
  ``-O3``; header handling, allocation and CPU setup use ``-Os``.
* Benchmark, logging and startup files use ``-Os``.

Native builds use the same split. Use ``-DMPEG_SPACE_OPTIMIZE=OFF`` in a separate
build directory for an all-``-O3`` control build.

**************
Resource usage
**************

Memory
======

The standalone XU316 examples built with XTC 15.3.1 and the default selective
optimisation profile report:

.. list-table:: Tile-1 memory, including the provisional stack reservation (bytes)
   :header-rows: 1

   * - Backend
     - Code
     - Data
     - Stack
     - Total
     - Remaining
   * - PL_MPEG
     - 29718
     - 401906
     - 16532
     - 448156
     - 76132
   * - libmpeg2
     - 63546
     - 427606
     - 16532
     - 507684
     - 16604

Each tile has 524288 bytes (512 KiB) of SRAM. Tile 0 holds about 4052 bytes of
startup and runtime state and no decoder code or data. The libmpeg2 figures are
for a build with ``MPEG_BENCH_ALLOW_MPEG1=ON``.

These are standalone fits, not proof that USB, audio and display work can be
added on the same tile. In particular there is not room for a full RGB888
framebuffer alongside the decoder; prefer converting YUV420 to RGB565 in strips
when integrating display output.

The PL_MPEG arena peaked at 379360 bytes, including allocation guards, of its
393216-byte capacity in native tests and at 379288 bytes on xcore. The libmpeg2
arena peaked at 397888 bytes of 409600.

Compared with an all-``-O3`` build of the same sources, selective optimisation
saves a modest amount of code and no picture-buffer memory:

.. list-table:: Tile-1 footprint by optimisation profile (bytes)
   :header-rows: 1

   * - Backend
     - All -O3
     - Selective -Os/-O3
     - Saving
   * - PL_MPEG
     - 449316
     - 448156
     - 1160
   * - libmpeg2
     - 511556
     - 507684
     - 3872

Stack
=====

Automatic stack analysis is incomplete. The push-input API installs no
callbacks, but upstream contains indirect callback branches that the tools
cannot size. Both examples therefore reserve 4096 words (16 KiB) explicitly, and
the tools' function-pointer warning is suppressed: for the PL_MPEG decoder
translation unit only in the default build, and for the whole library and
example in a libmpeg2 build. The
manual reservation is not a proved worst-case bound; complete the analysis
before production use.

***********
Performance
***********

All figures below are from the xsim simulator for 320 x 240 streams, with one
decoder task on tile 1 of ``XK-EVK-XU316`` and no USB, display or audio tasks
running. They were measured before the PL_MPEG fork's speed optimisations, and
on short synthetic clips; they have not been re-measured since. This library's
own benchmarks have not been run on hardware.

End-to-end playback has been measured on hardware in the ``lib_xuh``
``app_xuh_msc`` example (USB storage to an SPI LCD): 18.23 FPS on a 3025-frame,
320 x 240 file, with a mean decode time of 36.9 ms a frame. On that real content
the fork's optimisations reduced decode time from 40.6 ms. An application built
with ``-mno-dual-issue`` would otherwise pass that flag on to the decoder (49.4
ms a frame in the same test), so the library requests dual issue explicitly.

Matched comparison
==================

``tests/matched_benchmark.py`` generates one 320 x 240, 25 FPS YUV420
``testsrc2`` source and encodes it as MPEG-1 and MPEG-2 with one encoding
thread, GOP 12, two B pictures, qscale 6 and scene-cut insertion disabled. It
verifies the resulting I/P/B structure with ffprobe and writes a JSON report
with source, stream and image hashes, tool versions, settings, decoded hashes
and quality checks.

Three cases are run: PL_MPEG decoding MPEG-1, libmpeg2 decoding the **identical
MPEG-1 bitstream**, and libmpeg2 decoding the matched-source MPEG-2 stream. This
separates decoder implementation differences from codec differences. Equal
qscale is not a claim of equal bitrate, quality or coding work.

All cases read 4096-byte chunks. The timed ``codec_ns`` metric covers feed or
buffer-bind calls plus decode calls, including required copies and the
end-of-stream drain. File I/O, hashing, colour conversion, output and open/close
are excluded. Each case must match FFmpeg output for its bitstream to at least
45 dB PSNR with a maximum sample error of 16, and the xsim run must reproduce
the native frame count and hash.

.. list-table:: Matched three-picture I/P/B comparison (xsim FPS, including feed work)
   :header-rows: 1

   * - Decoder / input
     - All -O3
     - Selective -Os/-O3
   * - PL_MPEG / MPEG-1
     - 35.40
     - 35.52
   * - libmpeg2 / identical MPEG-1
     - 53.82
     - 53.79
   * - libmpeg2 / matched MPEG-2
     - 52.36
     - 52.10

All cases passed their checks. The difference between optimisation profiles is
below 0.5% on this short fixture.

To reproduce, build uninstrumented native reference executables::

    source .venv/bin/activate
    cmake -S . -B build-space-pl-native -DMPEG_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build build-space-pl-native -j 8
    cmake -S . -B build-space-lib-native -DMPEG_BACKEND=libmpeg2 \
      -DMPEG_SANITIZE=OFF -DMPEG_BENCH_ALLOW_MPEG1=ON -DCMAKE_BUILD_TYPE=Release
    cmake --build build-space-lib-native -j 8

and run the comparison::

    .venv/bin/python tests/matched_benchmark.py \
      --plmpeg-bench build-space-pl-native/mpeg_bench \
      --libmpeg2-bench build-space-lib-native/mpeg2_bench \
      --work build-matched-native/fixtures --label native-selective

For the target comparison, build both examples in separate directories, passing
``-DMPEG_BENCH_ALLOW_MPEG1=ON`` to the libmpeg2 example, then add
``--plmpeg-xe <pl-build>/bin/app_plmpeg_benchmark.xe`` and
``--libmpeg2-xe <lib-build>/bin/app_mpeg2_benchmark.xe`` to the command. Use
``-DMPEG_SPACE_OPTIMIZE=OFF`` builds for the all-``-O3`` column.

Each xsim case is bounded to 600000000 cycles and 180 host seconds. The default
six-picture comparison reached that timeout when both profiles were run
concurrently, so run profiles serially and use ``--frames 3``, which keeps I/P/B
coverage. A timed-out case is reported as ``xsim_status=TIMEOUT`` and has no
valid target rate.

Decode-only results
===================

Earlier runs measured decoder calls only, excluding feed copies. They are not
comparable with the matched ``codec_ns`` figures above or with each other, since
they use different streams.

.. list-table:: Decode-only xsim results
   :header-rows: 1

   * - Backend
     - Fixture
     - Decoder time
     - Rate
     - Output hash
   * - PL_MPEG
     - 6 pictures, MPEG-1
     - 155.868 ms
     - 38.49 FPS
     - ``a24768f4``
   * - libmpeg2
     - 24 pictures, MPEG-2
     - 349.222 ms
     - 68.72 FPS
     - ``42c7355d``

Both fixtures are ``testsrc2`` encoded at 25 FPS with ``-g 12 -bf 2 -q:v 6``,
and both hashes match native output. The PL_MPEG run was made before selective
optimisation; its worst ``mpeg_next()`` call took 35.057 ms. The libmpeg2 mean
I/P/B decode times were 32.742/13.143/11.431 ms. The libmpeg2 run predates the
explicit stack reservation and should be repeated before being treated as a
baseline.

***********
Correctness
***********

PL_MPEG
=======

The adapter and the PL_MPEG fork it compiles are tested together against a
native build of unmodified upstream PL_MPEG (``third_party/pl_mpeg_upstream``),
and must match it byte for byte, with one deliberate difference. At the pinned commit, upstream omits the last delayed
anchor picture when the final coded picture is a B picture. The adapter detects
that end-of-stream state and releases the pending picture exactly once. This
reads PL_MPEG private state and is done in the adapter, not in the fork.
Regression tests require all 24 pictures, including the extra final one, for
every refill size and across 30 repeat decodes of each fixture.

PL_MPEG is not bit-exact with FFmpeg's integer reconstruction. The 24-picture
fixtures achieved 54.96 dB PSNR (maximum sample error 12) for I/P and 55.80 dB
(maximum error 7) with B pictures. Tests require at least 45 dB and a maximum
error of 16. These bounds describe the test fixtures, not a general conformance
guarantee.

PL_MPEG keeps its upstream double-precision timestamp bookkeeping; no
floating-point to fixed-point changes have been made.

libmpeg2
========

The native MPEG-2 tests compare against FFmpeg independently, with approximately
70 dB observed PSNR and a maximum sample error of four. The compressed chunk
buffer is bounded to 32768 bytes by a single checked build-time source
transformation, described under `Third-party decoders`_.

Other decoders evaluated
========================

TinyMPG was evaluated and rejected: its P-picture reconstruction failed the
FFmpeg correctness gate. It is not a backend or dependency of this library.
