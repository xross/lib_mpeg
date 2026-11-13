#!/usr/bin/env python3
# Copyright 2026 XMOS LIMITED.
# SPDX-License-Identifier: MIT
"""Independent FFmpeg oracle plus deterministic input-boundary regressions."""
import argparse
import math
from pathlib import Path
import re
import shutil
import subprocess


def run(*args, ok=True):
    result = subprocess.run([str(arg) for arg in args], capture_output=True, text=True)
    if ok and result.returncode:
        raise AssertionError(f"{args}\n{result.stdout}\n{result.stderr}")
    assert "runtime error:" not in result.stderr and "ERROR: AddressSanitizer" not in result.stderr, result.stderr
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bench", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--allow-mpeg1", action="store_true")
    args = parser.parse_args()
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("FFmpeg is required for reference tests")
    args.work.mkdir(parents=True, exist_ok=True)
    for bframes in (0, 2):
        stream = args.work / f"ipb{bframes}.m2v"
        run(ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
            "testsrc2=size=320x240:rate=25", "-frames:v", 24, "-an", "-c:v", "mpeg2video",
            "-pix_fmt", "yuv420p", "-g", 12, "-bf", bframes, "-q:v", 6, "-f", "mpeg2video", stream)
        reference = args.work / f"reference{bframes}.yuv"
        run(ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", stream,
            "-an", "-pix_fmt", "yuv420p", "-f", "rawvideo", reference)
        expected = reference.read_bytes()
        assert len(expected) == 24 * 320 * 240 * 3 // 2
        baseline = None
        for chunk in (1, 7, 4093, 4096):
            output = args.work / f"decoded{bframes}-{chunk}.yuv"
            result = run(args.bench, stream, output, chunk)
            assert "frames=24" in result.stdout and "status=OK" in result.stdout, result.stdout
            decoded = output.read_bytes()
            if baseline is None:
                baseline = decoded
            assert decoded == baseline, f"Chunk-dependent output: {bframes=} {chunk=}"
            assert len(decoded) == len(expected)
            errors = [abs(a - b) for a, b in zip(decoded, expected)]
            mse = sum(e * e for e in errors) / len(errors)
            psnr = 10 * math.log10(255 * 255 / mse) if mse else math.inf
            assert psnr >= 45, f"Reference mismatch: {psnr=}"
            assert max(errors) <= 4, f"Large reference error: {max(errors)}"
            if chunk == 4096:
                counts = {kind: int(count) for kind, count in re.findall(r"([IPB]) pictures=(\d+)", result.stdout)}
                assert sum(counts.values()) == 24 and counts["I"] and counts["P"]
                assert bool(counts["B"]) == bool(bframes)
                print(result.stdout, end="")
                print(f"FFmpeg reference: PSNR={psnr:.2f} dB max_error={max(errors)}")
    empty = args.work / "empty.m2v"
    empty.write_bytes(b"")
    assert run(args.bench, empty, ok=False).returncode != 0
    invalid = args.work / "invalid.m2v"
    invalid.write_bytes(b"not an MPEG elementary stream")
    assert run(args.bench, invalid, ok=False).returncode != 0
    for codec, size, flags in (("mpeg2video", "640x480", []),
                                ("mpeg1video", "320x240", []),
                                ("mpeg2video", "320x240", ["-flags", "+ilme+ildct"])):
        if codec == "mpeg1video" and args.allow_mpeg1:
            continue
        unsupported = args.work / "unsupported.m2v"
        run(ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
            f"testsrc2=size={size}:rate=25", "-frames:v", 3, "-an", "-c:v", codec,
            *flags, "-pix_fmt", "yuv420p", "-f", codec, unsupported)
        assert run(args.bench, unsupported, ok=False).returncode != 0
    oversized = args.work / "oversized-chunk.m2v"
    data = stream.read_bytes()
    gop = data.index(b"\x00\x00\x01\xb8")
    oversized.write_bytes(data[:gop] + b"\x00\x00\x01\xb2" + b"A" * 40000 + data[gop:])
    assert run(args.bench, oversized, ok=False).returncode != 0
    print("PASS: MPEG-2 I/P/B, reference pixels, refill boundaries, format/size/chunk rejection")


if __name__ == "__main__":
    main()
