#############################
lib_mpeg: MPEG video decoding
#############################

************
Introduction
************

``lib_mpeg`` is an MPEG video decoder integration for XCORE devices.

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

Both decoders run on one thread. On XS3 the PL_MPEG backend's inverse DCT and
the separate RGB565 colour converter use the vector unit (VPU); the rest of that
decoder, and all of libmpeg2, is scalar C. The two decoder interfaces are not
interchangeable. The default backend exports a small
push-input API; the optional backend retains the upstream libmpeg2 API. A shared
backend interface is deferred until real player requirements establish it.

The default build does not compile or link libmpeg2. The upstream sources and
what has been changed in them are described under `Third-party decoders`_;
licensing is described in ``LICENSE.rst``.

*******************
The PL_MPEG adapter
*******************

PL_MPEG is a single-header desktop library: by default it allocates from the
heap, can read files through stdio, and grows its input buffer on demand. The
*adapter* is the XMOS-written layer that makes it usable on xcore and implements
the ``mpeg.h`` video API and the ``mpeg_audio.h`` MP2 audio API. PL_MPEG itself is compiled from the ``third_party/pl_mpeg``
submodule, a fork whose changes are speed optimisations; `Third-party decoders`_
lists them and says which ones alter decoded output.

.. list-table:: Adapter source files (``lib_mpeg/src/pl_mpeg``)
   :header-rows: 1

   * - File
     - Purpose
   * - ``mpeg_video.c``
     - Configures and includes ``pl_mpeg.h``; implements ``mpeg_open()``,
       ``mpeg_feed()``, ``mpeg_next()``, ``mpeg_close()`` and ``mpeg_audio_*()``.
       Video and audio share the translation unit because they use PL_MPEG's
       private implementation types.
   * - ``mpeg_memory.c``, ``mpeg_memory.h``
     - Bounded allocator, used in place of the heap, over the memory the
       application passes to each open call.

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
* ``PLM_VIDEO_IDCT`` selects the fork's inverse DCT. It is set from
  ``MPEG_VIDEO_IDCT`` by ``mpeg_config.h``; see `Configuration`_.

Replaces the heap with application memory
=========================================

Every PL_MPEG allocation is served from the memory the application passes to
``mpeg_open()`` or ``mpeg_audio_open()``. Each decoder has its own arena over
that memory. The allocator functions (``mpeg_memory_alloc()`` and related) are
internal to the library; applications do not provide or call them. PL_MPEG's
allocation macros carry no context, so every adapter entry point first selects
its arena for the calling logical core. A video and an audio decoder can
therefore run on different tasks of one tile, or on different tiles.

* Allocation is a simple bump allocator with 8-byte alignment and at most eight
  live blocks per arena, which is sufficient for one decoder. Space is reclaimed only when
  every block has been freed, that is when the decoder is closed.
* Each block is surrounded by 16 guard bytes. They are verified on every free and
  reported by ``mpeg_memory_stats()`` and ``mpeg_audio_memory_stats()``, which
  also give peak and live usage.
* A resize request that would grow a block, an allocation that does not fit, a
  corrupted guard or an unknown pointer calls ``abort()``. PL_MPEG does not check
  allocation results, so failing fatally is safer than returning a null pointer
  into it. The open calls and the stream checks below return ``MPEG_CAPACITY``
  for memory that is too small, to keep supported use away from these paths.

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

A failed check returns ``MPEG_UNSUPPORTED``. A stream whose pictures do not fit
in the memory passed to ``mpeg_open()`` returns ``MPEG_CAPACITY`` at the same
point. Like ``MPEG_ERROR``, it is latched:
every later call returns the same result until the decoder is closed. These
checks bound memory use for supported streams; they are not a general
malformed-stream validator.

End of stream
=============

A final ``mpeg_feed()`` call with ``size`` zero is valid and ends the stream.
The last picture in display order is returned whatever its coded type. Upstream
PL_MPEG gets both of these wrong; they are fixed in the fork, not worked around
in the adapter. See `Third-party decoders`_. A stream that ends without
producing any picture returns ``MPEG_ERROR``.

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

The fork's commits are ordered so that each group can be taken without the
ones after it: fixes, then portable optimisations, then xcore-only changes,
whose commit subjects carry an ``xcore:`` prefix. All xcore-specific code is in
the fork's ``xcore`` directory. ``pl_mpeg.h`` includes ``xcore/pl_mpeg_xcore.h``
on xcore targets and otherwise only names the hooks that header may define, so
the portable code has no xcore conditionals in it.

Two fixes change behaviour at the end of a stream:

* When the final coded picture is a B picture, upstream never returns the last
  reference picture, which is displayed after it. The fork returns it.
* Upstream does not register the end of input when it is signalled with nothing
  left in the buffer. The fork does.

The remaining changes affect decoding speed only. With the original transform
selected (``MPEG_VIDEO_IDCT_PLMPEG``) every picture upstream produces must be
identical, and these changes in ``pl_mpeg.h`` are always active:

* DCT coefficients are decoded from a 32-bit window of the bitstream instead of
  one bounds-checked call per bit, falling back to the original reads within 48
  bits of the end of the buffered data. A 256-entry table resolves the first
  eight bits of each coefficient code in one lookup.
* The original IDCT skips columns and rows whose only non-zero coefficient is
  the first.
* Motion compensation processes four pixels per 32-bit word, selects its
  interpolation/blending loop once per block and reuses a sliding window of
  source words. Adjacent horizontal predictions share loads. Address arithmetic,
  load sequencing and exact MPEG rounding are portable code in ``pl_mpeg.h``.
* Block output uses row pointers, skips constant blocks that add zero, and
  tests four pixels at once for clamping.
* Start codes are found by scanning the buffered data directly instead of one
  bounds-checked call per byte.
* A platform header may replace word loads and stores, full-pixel block copies
  and pixel stores. Without one the portable code is used.
* The portable word-access path supports unaligned buffers and both byte orders.
  Its final horizontal group reads only the one extra pixel needed, with no
  word-padding requirement. A platform may replace raw loads and extraction,
  state its word byte order and opt into padded full-word lookahead.
* On xcore, only those access mechanisms are specialised in
  ``xcore/pl_mpeg_xcore.h``: cache the alignment offset once per block, read/write
  aligned words, and use ``LEXTRACT`` for an unaligned pixel window. Frame memory
  must be aligned to four bytes, which the adapter's arena guarantees. On XS3,
  aligned full-pixel prediction uses the existing vector copy hook. The padded
  read contract is unchanged; there are no additional image buffers or tables.
  Half-pixel rounding and the separate bidirectional rounding step are unchanged.

Three further changes are optional, selected by the macros described under
`Configuration`_, and alter rounding:

* A 16-bit inverse DCT (``pl_mpeg_idct.h``, ``xcore/pl_mpeg_idct_xs3.S``). It is
  an orthonormal 8 x 8 transform with Q14 coefficients, computed as two passes
  of eight-term dot products on the XS3 vector unit, with a portable C model
  that gives the same bits. Coefficients are held as 16-bit values and transformed in
  place, so no conversion to and from the original 32-bit block is needed. It
  takes the same time for every block, where the original is faster on sparse
  blocks. Decoded pictures differ slightly from upstream; see `Correctness`_.
* The MP2 synthesis window accumulates with fused multiply-add
  (``pl_mpeg_audio.h``), using ``fmacc`` on XS3 and C99 ``fmaf`` elsewhere.
* MP2 PCM output is scaled by a reciprocal multiply instead of a divide.

The fixes and the always-active portable changes are intended for submission
upstream. No demux or timestamp-arithmetic changes have been made. The fork's ``tests`` directory
holds host tests for the transform and the synthesis window.

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

Configuration
=============

Options are compile-time macros whose defaults are in
``lib_mpeg/api/mpeg_config.h``. To change one, define it in an ``mpeg_conf.h``
header among the application's sources. XCommon CMake finds that header and the
library includes it; options it does not define keep their defaults.

.. code-block:: c

    /* mpeg_conf.h: decode with the original PL_MPEG transform */
    #define MPEG_VIDEO_IDCT MPEG_VIDEO_IDCT_PLMPEG

.. list-table:: Options for the PL_MPEG backend
   :header-rows: 1
   :widths: 25 25 50

   * - Option
     - Default
     - Effect
   * - ``MPEG_VIDEO_IDCT``
     - ``MPEG_VIDEO_IDCT_VPU16`` on XS3, ``MPEG_VIDEO_IDCT_SCALAR16`` elsewhere
     - Inverse DCT of the video decoder. ``MPEG_VIDEO_IDCT_VPU16`` is a 16-bit
       transform on the XS3 vector unit. ``MPEG_VIDEO_IDCT_SCALAR16`` is its
       bit-identical C model, used so that host builds reproduce target output;
       it is slower than the original and is not an optimisation.
       ``MPEG_VIDEO_IDCT_PLMPEG`` is the original PL_MPEG transform, the only
       one whose output is byte-identical to upstream.
   * - ``MPEG_RGB565_VPU``
     - 1
     - 1 converts to RGB565 on the XS3 vector unit, 0 with scalar lookup tables.
       The pixels are identical. Non-XS3 builds are always scalar.
   * - ``MPEG_AUDIO_FAST_SCALE``
     - 1
     - 1 scales MP2 PCM output by a reciprocal multiply, 0 by the original
       divide. Float rounding differs.
   * - ``MPEG_AUDIO_FAST_WINDOW``
     - 1
     - 1 uses fused multiply-accumulate in the MP2 synthesis window, 0 the
       original separate multiply and add. Float rounding differs.

These are C preprocessor options, not CMake cache settings. Use ``mpeg_conf.h``
for application configuration, or explicit compiler definitions in a dedicated
test build. ``MPEG_BACKEND`` chooses which sources are built and remains a CMake
setting. The fork source is always the pinned ``third_party/pl_mpeg`` submodule.

***
API
***

The default (PL_MPEG) video API is declared in ``lib_mpeg/api/mpeg.h``. The
application owns the decoder task and the decoder's memory, and performs file or
USB reads outside it:

#. Call ``mpeg_open(memory, size)``; expect ``MPEG_NEED_INPUT``.
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

    static uint8_t memory[MPEG_VIDEO_MEMORY_SIZE] __attribute__((aligned(8)));
    mpeg_frame_t frame;
    int eof = 0;

    mpeg_open(memory, sizeof(memory));
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

The application supplies the decoder's memory to ``mpeg_open()``: a buffer
aligned to eight bytes that it does not touch until ``mpeg_close()``. Declaring
it as a static array keeps its size in the build's memory report for the tile.
All decoder allocations come from it, including the three YUV420 pictures and
the ``MPEG_INPUT_CAPACITY`` (32 KiB) compressed buffer.

``MPEG_VIDEO_MEMORY_SIZE`` (384 KiB) is enough for any supported stream. A
player for smaller pictures can pass less: three pictures of 1.5 bytes per luma
sample, with width and height rounded up to multiples of 16, plus the input
buffer and about 2 KiB. For 160 x 120 that is about 124 KiB. ``mpeg_open()``
returns ``MPEG_CAPACITY`` if the memory cannot hold the decoder itself, and
``mpeg_feed()`` returns it if the stream's pictures do not fit.

There is no heap fallback and no mid-stream buffer growth. Unexpected upstream
allocation requests abort instead of returning a null pointer into upstream
code, which does not consistently check for it. ``mpeg_memory_stats()`` reports
the peak, live usage, capacity and guard status.

Only one video decoder can be open per tile. Picture pointers are tile-local and
read-only; do not pass them to another tile as if they were shared memory.

MP2 audio
=========

``mpeg_audio.h`` decodes MPEG-1 Audio Layer II using PL_MPEG's audio decoder. It
follows the same pattern as video, with its own memory, and is independent of
the video decoder: it may run on another task or another tile.

.. code-block:: c

    static uint8_t memory[MPEG_AUDIO_MEMORY_SIZE] __attribute__((aligned(8)));
    mpeg_audio_frame_t frame;

    mpeg_audio_open(memory, sizeof(memory));
    while ((n = read_audio(buffer, sizeof(buffer))) != 0) {
        mpeg_audio_feed(buffer, n);    /* n <= mpeg_audio_input_space() */
        while (mpeg_audio_next(&frame) == MPEG_FRAME)
            play(frame.samples, MPEG_AUDIO_FRAME_SAMPLES, frame.sample_rate);
    }
    if (mpeg_audio_finish() != MPEG_END) { /* failed or truncated */ }
    mpeg_audio_close();

* Input is an MP2 elementary stream starting on a frame header. The application
  demultiplexes ``.mpg`` program streams itself; the ``lib_xuh`` ``app_xuh_msc``
  example contains a reader that does.
* Each frame is ``MPEG_AUDIO_FRAME_SAMPLES`` (1152) interleaved left/right pairs
  of ``float`` in the range -1 to 1, valid until the next call. A mono stream is
  given on both channels.
* ``mpeg_audio_next()`` returns ``MPEG_NEED_INPUT`` until a whole frame is
  buffered. At most ``MPEG_AUDIO_INPUT_CAPACITY`` (4 KiB) is buffered, and
  ``MPEG_AUDIO_MEMORY_SIZE`` (28 KiB) of memory is required.
* Free-format, Layer I, Layer III and MPEG-2 audio are rejected. There is no
  resynchronisation: a damaged frame or a change of format is a terminal
  ``MPEG_ERROR``.
* One audio decoder can be open per tile. No output, pacing or audio/video
  synchronisation is provided.

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
shares no state with it. An odd width is rounded up to even.

XS3 builds use a fused VPU converter by default. It processes 16 pixels in each
of two rows, reusing chroma contributions across both rows. All six input/output
pointers must be 4-byte aligned for the vector path; other alignments, small
widths and tails use exact scalar arithmetic. Neither path reads beyond the
documented input buffers. The wrapper stages a padded 128-byte input block;
the assembly reserves 256 bytes of stack, including register saves, plus small
constant vectors in code memory. There are no lookup tables or extra image
buffers, and each task owns its scratch.

The VPU's rounded saturation is explicitly compensated to preserve the scalar
Q16 arithmetic floors. Channels are clipped and quantised to 5/6/5 bits, then
packed directly into high-byte-first output using raw accumulator low halves.
The exhaustive hardware test checks that SIMD lane order, byte duplication,
rounding and packing agree with unmodified upstream PL_MPEG for every colour.

Define ``MPEG_RGB565_VPU`` as 0 in ``mpeg_conf.h`` to select
the original tabulated scalar converter. Non-XS3 and native builds also use that
implementation. It allocates 5504 bytes of lookup arrays and a readiness flag;
its first call must run from one task. The native test suite additionally builds
a portable model of the VPU coefficient decomposition and rounding, but that
model does not validate instruction execution or measure xcore performance.

On XU316 at 600 MHz, XTC Tools 15.3.1, a single-task conversion-only benchmark
of 32 equivalent 320 x 240 frames measured **12684 us/frame scalar** versus
**5756 us/frame VPU**, a **2.20x** speedup. Both checksums were ``cb839b61``.
Input staging is included and the aligned full-width path is timed; channel
reception, decoding and physical SPI are not. The test prints only outside the
timed section, using JTAG I/O; these are not full-player FPS measurements.

In the default XU316 USB/LCD player workload, VPU conversion takes about
5.765 ms/frame. Channel reception, decoding and LCD output have separate timing
scopes and overlap; a converter speedup is not an equal full-player speedup.
See `Performance`_ for the current integrated workload and its limitations.

Run the same exhaustive test and conversion benchmark on hardware::

    cmake -G "Unix Makefiles" -S tests/app_rgb565 -B build-rgb565-vpu \
      -DMPEG_RGB565_VPU=ON
    xmake -C build-rgb565-vpu -j 8
    xrun --io build-rgb565-vpu/bin/test_mpeg_rgb565.xe

Use a separate build directory with ``-DMPEG_RGB565_VPU=OFF`` for the scalar
comparison. This finite test has no timing-sensitive USB/display tasks; its
JTAG output occurs before and after the timed loop. Do not use JTAG logging for
full-player throughput measurements, which require xSCOPE instead.

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

Native tests
============

From the repository root, the default PL_MPEG build and tests are::

    python3 -m venv .venv
    source .venv/bin/activate
    cmake -S tests -B build-plmpeg-native
    cmake --build build-plmpeg-native -j 8
    ctest --test-dir build-plmpeg-native --output-on-failure

The suite decodes 24-picture I/P and I/P/B clips with input refills of 1, 7,
4093 and 4096 bytes, and covers repeated decode/reopen cycles, empty and invalid
inputs, dimension and codec rejection, and capacity limits. Output is compared
with a pristine upstream build and with FFmpeg; see `Correctness`_.

Host builds decode with ``MPEG_VIDEO_IDCT_SCALAR16``, the model of the target's
transform, so the suite checks the arithmetic the target runs. Configure a
separate build directory with ``-DMPEG_TEST_ORIGINAL_IDCT=ON`` to run the same suite
on the original transform, where output must match upstream byte for byte.

``plmpeg_idct_test`` and ``plmpeg_window_test`` come from the fork. The first
checks the transform the decoder calls against the exact transform (maximum
error 1) on single-coefficient, extreme and random blocks, with canaries around
the block; the second checks the fused synthesis window against the original
loops for every history phase. Build the transform test for xcore to check the
vector implementation against the C model bit for bit and to time it against
the original (XTC Tools active)::

    cmake -G "Unix Makefiles" -S tests/app_idct -B build-idct-xcore
    xmake -C build-idct-xcore -j 8
    xsim build-idct-xcore/bin/vpu16/test_mpeg_idct_vpu16.xe
    xsim build-idct-xcore/bin/plmpeg/test_mpeg_idct_plmpeg.xe

``mpeg_motion_test`` additionally checks the fork's actual macroblock function
against an independent per-pixel reference in 2128 interior cases. It covers 8- and
16-pixel blocks, every source alignment, positive and negative integer/half-pixel
motion, all interpolation modes and bidirectional averaging, using zero, 255,
alternating and deterministic random data. The entire destination is checked,
including bytes outside the written block. Plane-edge checks add 326656 native
cases covering all source/destination byte alignments, with exact-size source
allocations so ASan detects excess tail reads. The same test runs against
big-endian word-access hooks in ``mpeg_motion_big_endian_test``. The standalone
xcore build checks 20416 plane-edge cases with its aligned, padded-buffer contract.
``mpeg_api_test`` also covers the
MP2 API: rejected headers and formats, truncation, bounded feeds, and memory
that is null, misaligned or too small for either decoder. Build the motion test for xcore to
exercise the aligned-load/LEXTRACT implementation rather than the native
memcpy path (XTC Tools active)::

    cmake -G "Unix Makefiles" -S tests/app_motion -B build-motion-xcore
    xmake -C build-motion-xcore -j 8
    xrun --io build-motion-xcore/bin/test_mpeg_motion.xe

This finite standalone test has no real-time USB/display tasks. JTAG output is
used only for the final pass/fail report, not a throughput measurement.

The optional MPEG-2 tests use a separate build directory::

    cmake -S tests -B build-libmpeg2-native -DMPEG_BACKEND=libmpeg2
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

    xrun --xscope build-plmpeg-xu316/bin/app_plmpeg_benchmark.xe

Unlike the USB player, this benchmark needs host-file syscalls serviced as well
as terminal output. Do not add ``--xscope-io-only`` to this host-file run.

For a small checked simulator smoke test, generate the deterministic clip and
run it through ``tests/run_xsim.py``::

    ffmpeg -hide_banner -loglevel error -y -f lavfi \
      -i testsrc2=size=320x240:rate=25 -frames:v 6 -an \
      -c:v mpeg1video -pix_fmt yuv420p -g 12 -bf 2 -q:v 6 \
      -f mpeg1video build-plmpeg-native/fixtures/VIDEO.M1V
    .venv/bin/python tests/run_xsim.py \
      --xe build-plmpeg-xu316/bin/app_plmpeg_benchmark.xe \
      --work build-plmpeg-native/fixtures --frames 6 --hash 9bdf6ed3

The runner bounds xsim to 600000000 cycles and a 180 s process timeout. Success
requires normal completion, the expected picture count and hash, and
``status=OK``. The hash is that of the default transform and equals the native
build's; with the original transform selected in ``mpeg_conf.h`` it is
``a24768f4``. Reaching either bound is a failure, not a result.

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

Each XU316 tile has 524288 bytes (512 KiB) of SRAM. The application supplies
384 KiB for maximum-resolution PL_MPEG video, including its 32-KiB compressed
input buffer, and 28 KiB for MP2 audio. Smaller supported pictures can use less
video memory; query the allocator's peak and guard status for the actual input.
The optional libmpeg2 backend has a separate 400-KiB guarded arena.

Code, other application buffers and all task stacks must fit alongside those
arenas. Do not budget a full RGB888 framebuffer beside a 320 x 240 decoder on
one tile; convert YUV420 to RGB565 in strips on the display tile instead.
The default USB/LCD player uses 332516 bytes on tile 0 and 482188 bytes on tile 1,
including its provisional stack reservation, with a video arena peak of 379176
bytes. These application-specific totals are not the library's standalone
footprint or a budget for arbitrary extra tasks. Inspect the linker's ``-report``
output whenever changing features, buffers, targets or compiler settings.

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

Performance depends on picture content, input delivery, display backpressure
and the other tasks on each tile. Kernel timings, standalone decoder timings
and completed-player FPS measure different work and are not interchangeable.
The library requests dual issue explicitly for its decoder sources so an
application's ``-mno-dual-issue`` does not silently disable it there.

Inverse DCT
===========

``tests/app_idct`` checks the transform the decoder calls against its scalar
model, with basis/extreme/random blocks, padding and canaries. It also times
dense dequantized blocks and blocks with four low-frequency coefficients set.
On XU316 hardware at 600 MHz, using XTC Tools 15.3.1:

.. list-table:: Transform time per 8 x 8 block (hardware)
   :header-rows: 1

   * - ``MPEG_VIDEO_IDCT``
     - Dense
     - Sparse
   * - ``MPEG_VIDEO_IDCT_PLMPEG``
     - 13.274 us
     - 10.774 us
   * - ``MPEG_VIDEO_IDCT_VPU16``
     - 2.558 us
     - 2.558 us

These are single-task kernel timings, with JTAG logging outside the timed
sections, not decoder or player frame rates. Build and run the finite test
with XTC Tools active::

    cmake -G "Unix Makefiles" -S tests/app_idct -B build-idct-validation-hardware
    xmake -C build-idct-validation-hardware -j 8
    xrun --adapter-id SERIAL --io \
      build-idct-validation-hardware/bin/vpu16/test_mpeg_idct_vpu16.xe
    xrun --adapter-id SERIAL --io \
      build-idct-validation-hardware/bin/plmpeg/test_mpeg_idct_plmpeg.xe

USB/LCD integration
===================

The default decoder in ``lib_xuh/examples/app_xuh_msc`` runs a 3025-picture,
320 x 240 reference clip at **33.59 FPS**, with mean decode time **19.137 ms/frame**.
The workload uses XU316 at 600 MHz, XTC Tools 15.3.1, ILI9341 at 50 MHz SPI,
VPU IDCT and RGB565 conversion, read-ahead/multi-sector storage reads, early YUV
acknowledgement, FPS overlay and activity LED. Audio and timing histograms are
disabled. This is one integrated workload, not a standalone decoder rate or a
guaranteed worst-case 30-FPS capability.

For this clip the default transform's Y/Cb/Cr PSNR against FFmpeg is
55.59/62.00/63.63 dB, with maximum sample errors 5/3/3. The transform intentionally
changes rounding; it is not byte-identical to the original PL_MPEG transform.
Optional timing histograms report decode and input/feed/decode/handoff
service-period percentiles and maxima. Collect these with the current build
and input; they are not guaranteed execution bounds. Service periods are
unpaced YUV-handoff intervals, not physical LCD timestamps or missed
presentation-deadline counts.

The application's ``doc/rst/pipeline.rst`` documents its input formats, build
configurations, ownership protocol, stage timings and physical display wiring.
The cross-tile checksum-renderer fixture tests picture hashes, ownership,
EOF/repeat and error recovery without modelling physical USB or LCD throughput.

Codec and USB/LCD validation
============================

All native decoder checks are part of the ``tests`` CMake project; none needs
``lib_xuh`` or another application checkout. Generated elementary-stream fixtures
are the default. For full-content validation, set ``MPEG_TEST_VIDEO_STREAM`` to
an MPEG-1 elementary video file and ``MPEG_TEST_AUDIO_STREAM`` to an elementary
MP2 audio file when configuring the same native test build.

The MP2 reference check compares baseline PCM bit for bit with pristine
upstream and bounds the reciprocal-scale/fused-window variants to a peak
difference of 1e-6 and an SNR of at least 120 dB. It feeds raw MP2 directly into
``mpeg_audio_*()`` and checks refill boundaries, replay, malformed headers and
truncation. PS demultiplexing is application-owned and is not part of these
codec tests. The video comparator checks
sample counts, per-plane error, PSNR and first/final windows against pristine
PL_MPEG and FFmpeg. Target kernel tests check vector execution against the scalar
model; native models alone do not validate hardware instructions.

``tests/usb_lcd`` contains only end-to-end hardware integration tools for the
external USB-storage/LCD application, not another decoder or native test project.
``tests/usb_lcd/compare_mpeg_idct.py --help`` describes the two-build USB/LCD
comparison. It validates complete reports, frame/byte counts, EOF status and
arena cleanup, records input/executable hashes and compares completed plays
after startup. It reuses ``tests/usb_lcd/run_hardware.py``, which also provides a bounded smoke
runner for the video/MP2 player. Hardware comparisons require an idle XTAG and
the appropriate input already on the USB stick; neither runner writes flash or
USB media. Use xSCOPE for the real player, not JTAG logging.

Standalone backend comparison
=============================

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

Build uninstrumented native reference executables::

    source .venv/bin/activate
    cmake -S tests -B build-space-pl-native -DMPEG_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build build-space-pl-native -j 8
    cmake -S tests -B build-space-lib-native -DMPEG_BACKEND=libmpeg2 \
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

Each xsim case is bounded to 600000000 cycles and 180 host seconds. Run profiles
serially; ``--frames 3`` retains I/P/B coverage with a smaller simulation load.
A timed-out case is reported as ``xsim_status=TIMEOUT`` and has no valid target
rate. Simulator wall time is not target time, and short synthetic decoder rates
are not a prediction of integrated playback performance.

***********
Correctness
***********

PL_MPEG
=======

The adapter and the PL_MPEG fork it compiles are tested together against a
native build of unmodified upstream PL_MPEG (``third_party/pl_mpeg_upstream``).

With ``MPEG_VIDEO_IDCT_PLMPEG`` the output must match upstream byte for byte,
with one deliberate difference. Upstream omits the last reference picture when
the final coded picture is a B picture, which the fork fixes. Regression tests
require all 24 pictures, including the extra final one, for every refill size
and across 30 repeat decodes of each fixture, and compare every picture
upstream does produce.

With the default 16-bit transform the same tests run, but pictures are not
byte-identical to upstream because the transform rounds differently. They must
instead stay within a mean squared error of 1 and a maximum sample error of 16
of upstream. The xcore vector transform is tested bit for bit against the C
model that host builds decode with, and the xsim smoke test requires the
target's output hash to equal the host's.

Neither transform is bit-exact with FFmpeg's integer reconstruction, so FFmpeg
is used as an independent quality bound:

.. list-table:: 24-picture fixtures against FFmpeg
   :header-rows: 1

   * - ``MPEG_VIDEO_IDCT``
     - I/P
     - I/P/B
     - Required
   * - ``MPEG_VIDEO_IDCT_SCALAR16`` / ``_VPU16``
     - 57.75 dB, maximum error 4
     - 58.26 dB, maximum error 4
     - 50 dB, maximum error 8
   * - ``MPEG_VIDEO_IDCT_PLMPEG``
     - 54.96 dB, maximum error 12
     - 55.80 dB, maximum error 7
     - 45 dB, maximum error 16

These bounds describe the test fixtures, not a general conformance guarantee.

PL_MPEG keeps its upstream double-precision timestamp bookkeeping; no
floating-point to fixed-point changes have been made.

libmpeg2
========

The native MPEG-2 tests compare against FFmpeg independently, with approximately
70 dB observed PSNR and a maximum sample error of four. The compressed chunk
buffer is bounded to 32768 bytes by a single checked build-time source
transformation, described under `Third-party decoders`_.
