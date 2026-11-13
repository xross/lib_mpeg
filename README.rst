:orphan:

#############################
lib_mpeg: MPEG video decoding
#############################

:vendor: xross
:version: 0.1.0
:scope: Experimental
:description: Experimental scalar MPEG video decoding for XCORE with selectable backends
:category: Vision
:keywords: MPEG-1, MPEG-2, video, decoder, xcore
:devices: xcore.ai

*******
Summary
*******

``lib_mpeg`` is an experimental MPEG video decoder for XCORE devices. It is a
decoder bring-up and evaluation, not a complete media player.

The default backend is `PL_MPEG <https://github.com/phoboslab/pl_mpeg>`_, decoding
MPEG-1 video elementary streams up to 320 x 240 through a small push-input API
with a fixed static memory arena. An optional
`libmpeg2 <https://libmpeg2.sourceforge.io/>`_ backend can be selected instead to
evaluate MPEG-2 decoding. Only one backend is compiled into a build.

Both backends are scalar C running on one logical core. Build instructions, the
API, resource usage and benchmark results are in the user guide,
``doc/rst/lib_mpeg.rst``.

********
Features
********

* MPEG-1 video decoding (I, P and B pictures) to progressive YUV420, up to 320 x 240.
* Push-input API: the application owns storage I/O and feeds compressed bytes.
* Fixed 384 KiB static decoder arena and 32 KiB compressed-input buffer; no heap use.
* Optional libmpeg2 backend for MPEG-2 evaluation, with a 400 KiB guarded arena.
* Standalone XU316 benchmark examples for each backend with decode timing.
* Native reference tests checked against pristine upstream and FFmpeg output.
* Pinned, unmodified upstream submodules.

************
Known issues
************

* No VPU acceleration or parallel decoding; decode speed has only been measured
  in the simulator, not on hardware.
* Video elementary streams (``.m1v``) only. Multiplexed ``.mpg`` program streams
  and MP2 audio are not supported.
* No colour conversion, display, storage or audio output is included.
* Trusted input only: this is not a hardened validator for malformed or truncated
  streams.
* One decoder per tile. Resolution changes, concatenated streams and seeking are
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

* None. The upstream decoders are pinned Git submodules under ``third_party``;
  clone with ``--recurse-submodules``.

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
stays GPL-compatible, and ``third_party/README.rst`` for upstream pins.

*******
Support
*******

This is experimental software provided without formal support. Issues can be
raised using GitHub `issues <https://github.com/xross/lib_mpeg/issues>`_.
