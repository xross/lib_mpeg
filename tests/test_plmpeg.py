#!/usr/bin/env python3
# Copyright 2026 XMOS LIMITED.
# This Software is subject to the terms of the XMOS Public Licence: Version 1.
"""Native reference tests for the bounded PL_MPEG video adapter."""
import argparse
import math
from pathlib import Path
import re
import shutil
import subprocess


def run(*args, ok=True):
    result = subprocess.run([str(arg) for arg in args], capture_output=True, text=True, timeout=30)
    if ok and result.returncode:
        raise AssertionError(f"{args}\n{result.stdout}\n{result.stderr}")
    assert "runtime error:" not in result.stderr and "ERROR: AddressSanitizer" not in result.stderr, result.stderr
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bench", type=Path, required=True)
    parser.add_argument("--api-test", type=Path, required=True)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("FFmpeg is required")
    args.work.mkdir(parents=True, exist_ok=True)
    for bframes in (0, 2):
        stream = args.work / f"ipb{bframes}.m1v"
        run(ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
            "testsrc2=size=320x240:rate=25", "-frames:v", 24, "-an", "-c:v", "mpeg1video",
            "-pix_fmt", "yuv420p", "-g", 12, "-bf", bframes, "-q:v", 6, "-f", "mpeg1video", stream)
        reference = args.work / f"reference{bframes}.yuv"
        run(ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", stream,
            "-an", "-pix_fmt", "yuv420p", "-f", "rawvideo", reference)
        expected = reference.read_bytes()
        assert len(expected) == 24 * 320 * 240 * 3 // 2
        upstream = args.work / f"upstream{bframes}.yuv"
        run(args.upstream, stream, upstream)
        upstream_pixels = upstream.read_bytes()
        # The pinned pristine decoder omits the delayed final anchor when the
        # last coded picture is B. Our adapter explicitly drains that picture.
        picture_bytes = 320 * 240 * 3 // 2
        assert len(upstream_pixels) == len(expected) - (picture_bytes if bframes else 0)
        baseline = None
        for chunk in (1, 7, 4093, 4096):
            output = args.work / f"decoded{bframes}-{chunk}.yuv"
            result = run(args.bench, stream, output, chunk)
            assert "frames=24" in result.stdout and "status=OK" in result.stdout, result.stdout
            decoded = output.read_bytes()
            assert len(decoded) == len(expected)
            assert decoded[:len(upstream_pixels)] == upstream_pixels, "Adapter differs from pristine upstream PL_MPEG"
            if baseline is None:
                baseline = decoded
            assert decoded == baseline, f"Refill-dependent output: {bframes=} {chunk=}"
            errors = [abs(a - b) for a, b in zip(decoded, expected)]
            mse = sum(e * e for e in errors) / len(errors)
            psnr = 10 * math.log10(255 * 255 / mse) if mse else math.inf
            # Require exact agreement with the pristine implementation above.
            # Its integer reconstruction is not bit-exact to FFmpeg; retain an
            # independent quality bound as well as the bit-exact port check.
            assert psnr >= 45 and max(errors) <= 16, (psnr, max(errors))
            if chunk == 4096:
                print(result.stdout, end="")
                print(f"FFmpeg reference: PSNR={psnr:.2f} dB max_error={max(errors)}")
                assert int(re.search(r"arena_peak=(\d+)", result.stdout)[1]) <= 384 * 1024
        run(args.api_test, stream)
    for name, data in (("empty", b""), ("invalid", b"not MPEG video"),
                       ("header-only", b"\0\0\1\xb3\x14\x00\xf0\x13")):
        stream = args.work / f"{name}.m1v"
        stream.write_bytes(data)
        assert run(args.bench, stream, ok=False).returncode != 0
    for codec, size in (("mpeg1video", "640x480"), ("mpeg2video", "320x240")):
        stream = args.work / f"unsupported-{codec}.m1v"
        run(ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
            f"testsrc2=size={size}:rate=25", "-frames:v", 3, "-an", "-c:v", codec,
            "-pix_fmt", "yuv420p", "-f", codec, stream)
        assert run(args.bench, stream, ok=False).returncode != 0
    # First-picture lookahead must fail within the compressed capacity, not grow
    # the upstream buffer or corrupt the following allocation.
    data = (args.work / "ipb2.m1v").read_bytes()
    picture = data.index(b"\0\0\1\0")
    oversized = args.work / "oversized.m1v"
    oversized.write_bytes(data[:picture] + b"\0\0\1\xb2" + b"A" * 40000 + data[picture:])
    assert run(args.bench, oversized, ok=False).returncode != 0
    print("PASS: MPEG-1 I/P/B, FFmpeg pixels, refill boundaries, 60 decode cycles, input/size/capacity errors")


if __name__ == "__main__":
    main()
