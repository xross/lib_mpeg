:orphan:

#############################
lib_mpeg: MPEG video decoding
#############################

:vendor: xross
:version: 0.1.0
:scope: Experimental
:description: MPEG video decoding for XCORE with selectable backends
:category: Vision
:keywords: MPEG-1, MPEG-2, video, decoder
:devices: xcore.ai

*******
Summary
*******

``lib_mpeg`` is an experimental MPEG video and MP2 audio decoder for XCORE devices.

The default backend is `PL_MPEG <https://github.com/phoboslab/pl_mpeg>`_, decoding
MPEG-1 video elementary streams up to 320 x 240, and MP2 audio, through small
push-input APIs that use memory supplied by the application. An optional
`libmpeg2 <https://libmpeg2.sourceforge.io/>`_ backend can be selected instead to
evaluate MPEG-2 decoding. Only one backend is compiled into a build.

Both backends run on one thread. On XS3 the default backend's inverse DCT and
the colour converter use the vector unit; the rest is scalar C.

********
Features
********

* MPEG-1 video decoding (I, P and B pictures) to progressive YUV420, up to 320 x 240.
* Push-input API: the application owns storage I/O and feeds compressed bytes.
* MP2 (MPEG-1 Audio Layer II) decoding to interleaved stereo ``float`` samples,
  independent of the video decoder and usable on another task or tile.
* No heap use: the application passes each decoder its memory, 384 KiB for
  320 x 240 video (less for smaller pictures) and 28 KiB for audio.
* YUV 4:2:0 to RGB565 colour conversion for 16-bit displays.
* Bit-exact XS3 VPU colour conversion, with a scalar build option and no extra
  frame buffer.
* XS3 VPU inverse DCT, about five times faster per block than the original in
  simulation, with a bit-identical C model for host tests. The original PL_MPEG
  transform remains selectable.
* Options set in an application ``mpeg_conf.h`` header: inverse DCT, colour
  converter, and the MP2 decoder's fused synthesis window and reciprocal output
  scaling.
* Optional libmpeg2 backend for MPEG-2 evaluation, with a 400 KiB guarded arena.
* Standalone XU316 benchmark examples for each backend with decode timing.
* Native reference tests checked against unmodified upstream and FFmpeg output.
* PL_MPEG fork with speed optimisations; output is identical to upstream when the
  original transform is selected.

************
Known issues
************

* No parallelism within the decoder. Only the inverse DCT and the colour
  converter are VPU-accelerated, on XS3. With the default inverse DCT, decoded
  pictures are close to but not byte-identical with upstream PL_MPEG. The
  standalone hardware decoder rate is not established. The default decoder in
  the separate XU316/ILI9341 USB/LCD player runs a 320 x 240 reference clip at
  30.37 FPS, with mean decode time 22.90 ms/frame. This is one integrated
  workload, not a standalone decoder rate or a worst-case frame-time guarantee.
* Elementary streams only (``.m1v`` video, MP2 audio). Multiplexed ``.mpg``
  program streams must be demultiplexed by the application.
* No display, storage or audio output, and no audio/video synchronisation, is
  included; those are the application's responsibility.
* Trusted input only: this is not a hardened validator for malformed or truncated
  streams.
* One video and one audio decoder per tile. Resolution changes, concatenated streams and seeking are
  unsupported; close and reopen to replay.
* A compressed picture plus decoder lookahead must fit in the 32 KiB input buffer.
  Not every 320 x 240 encode fits; reduce encoder quality or bitrate if it does not.
* The example stack reservation (16 KiB) is provisional, not a proved worst-case
  bound.
* ``mpeg.h`` is an experimental API. The optional libmpeg2 backend uses its own
  ``mpeg2.h`` interface; the two are not interchangeable.
* Binaries built with the libmpeg2 backend are subject to GPL-2.0-or-later.

****************
Development repo
****************

* `lib_mpeg <https://github.com/xross/lib_mpeg>`_ (https://github.com/xross/lib_mpeg)

**************
Required tools
**************

* XMOS XTC Tools: 15.3.1

*********************************
Required libraries (dependencies)
*********************************

* None.

*************************
Related application notes
*************************

* None

*********
Licensing
*********

XMOS-authored sources are under the XMOS Public Licence: Version 1. PL_MPEG is
MIT-licensed and libmpeg2 is GPL-2.0-or-later. See ``LICENSE.rst`` for the full
terms, including the files that are MIT-licensed so the optional libmpeg2 build
stays GPL-compatible.

*******
Support
*******

This is experimental software provided without formal support. Issues can be
raised using GitHub `issues <https://github.com/xross/lib_mpeg/issues>`_.
