#!/usr/bin/env python3
# Copyright 2026 XMOS LIMITED.
# This Software is subject to the terms of the XMOS Public Licence: Version 1.
"""Matched source/GOP plus identical-MPEG-1-bitstream decoder comparisons.

Build native executables without sanitizers before measuring. libmpeg2 needs
MPEG_BENCH_ALLOW_MPEG1=ON. Optional xsim images must use the same configuration.
Quality failures remain failures and are excluded from performance rankings.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess


def run(command, timeout=60, cwd=None):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True,
                            timeout=timeout, cwd=cwd)
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")
    if "runtime error:" in result.stderr or "ERROR: AddressSanitizer" in result.stderr:
        raise RuntimeError(result.stderr)
    return result.stdout + result.stderr


def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return f"{value:08x}"


def metric(log, name):
    match = re.search(rf"\b{name}=([0-9]+)", log)
    if not match:
        raise RuntimeError(f"Missing {name} in benchmark output:\n{log}")
    return int(match[1])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--plmpeg-bench", type=Path, required=True)
    parser.add_argument("--libmpeg2-bench", type=Path, required=True)
    parser.add_argument("--plmpeg-xe", type=Path)
    parser.add_argument("--libmpeg2-xe", type=Path)
    parser.add_argument("--work", type=Path, default=Path("build-matched/fixtures"))
    parser.add_argument("--frames", type=int, default=6)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--label", default="unspecified", help="Build/profile label recorded in results")
    parser.add_argument("--sim-timeout", type=int, default=180)
    args = parser.parse_args()
    if not 1 <= args.frames <= 48 or not 1 <= args.repetitions <= 10 or args.sim_timeout < 1:
        parser.error("Use 1..48 frames and 1..10 native repetitions")
    if bool(args.plmpeg_xe) != bool(args.libmpeg2_xe):
        parser.error("Supply both PL_MPEG and libmpeg2 xsim images, or neither")
    ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
    if not ffmpeg or not ffprobe:
        parser.error("FFmpeg and ffprobe must be on PATH")
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = work / "source.y4m"
    run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
         "testsrc2=size=320x240:rate=25", "-frames:v", args.frames, "-pix_fmt", "yuv420p", source])
    report = {
        "label": args.label, "source": "testsrc2 320x240 YUV420 25 FPS", "frames": args.frames,
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "ffmpeg": run([ffmpeg, "-version"]).splitlines()[0],
        "settings": {"gop": 12, "bframes": 2, "qscale": 6, "scene_threshold": 1000000000},
        "timing": "sum of feed/buffer-bind and decode calls; excludes file I/O, hashing, output, open/close",
        "read_bytes": 4096, "results": [],
    }
    if args.plmpeg_xe:
        report["toolchain"] = run(["xcc", "--version"]).splitlines()[:2]
        report["target"] = "XK-EVK-XU316; single decoder task on tile 1; default 100 MHz reference timer"
    streams, references, patterns = {}, {}, {}
    for codec, extension in (("mpeg1video", "m1v"), ("mpeg2video", "m2v")):
        path = work / f"matched.{extension}"
        run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", source,
             "-an", "-c:v", codec, "-threads", 1, "-g", 12, "-bf", 2, "-q:v", 6,
             "-sc_threshold", 1000000000, "-f", codec, path])
        probe = json.loads(run([ffprobe, "-v", "error", "-show_entries", "frame=pict_type", "-of", "json", path]))
        patterns[codec] = [x["pict_type"] for x in probe["frames"]]
        if len(patterns[codec]) != args.frames:
            raise RuntimeError("Encoder picture count does not match source")
        ref = work / f"reference-{codec}.yuv"
        run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", path,
             "-pix_fmt", "yuv420p", "-f", "rawvideo", ref])
        references[codec] = ref.read_bytes()
        streams[codec] = path
    if patterns["mpeg1video"] != patterns["mpeg2video"]:
        raise RuntimeError(f"Unmatched picture structure: {patterns}")
    report["picture_structure"] = patterns["mpeg1video"]
    report["stream_sha256"] = {k: hashlib.sha256(v.read_bytes()).hexdigest() for k, v in streams.items()}
    cases = [
        ("pl_mpeg-mpeg1", "mpeg1video", args.plmpeg_bench, args.plmpeg_xe, "VIDEO.M1V"),
        ("libmpeg2-mpeg1", "mpeg1video", args.libmpeg2_bench, args.libmpeg2_xe, "VIDEO.M2V"),
        ("libmpeg2-mpeg2", "mpeg2video", args.libmpeg2_bench, args.libmpeg2_xe, "VIDEO.M2V"),
    ]
    failures = 0
    for name, codec, executable, xe, input_name in cases:
        row = {"case": name, "codec": codec, "native_codec_ns": [],
               "native_executable": str(executable.resolve())}
        report["results"].append(row)
        output = work / f"{name}.yuv"
        log = run([executable.resolve(), streams[codec], output, 4096])
        (work / f"{name}-native.log").write_text(log)
        pixels = output.read_bytes()
        expected = references[codec]
        if metric(log, "frames") != args.frames or len(pixels) != len(expected) or "status=OK" not in log:
            raise RuntimeError(f"Incomplete decode: {name}\n{log}")
        errors = [abs(a-b) for a, b in zip(pixels, expected)]
        mse = sum(e*e for e in errors) / len(errors)
        psnr = 10 * math.log10(255*255/mse) if mse else 999.0
        row.update(psnr_db=psnr, max_error=max(errors), hash=fnv(pixels), arena_peak=metric(log, "arena_peak"))
        if psnr < 45 or max(errors) > 16:
            row["status"] = "REJECTED: pixel correctness threshold"
            failures += 1
            print(f"{name}: REJECTED (PSNR={psnr:.2f}, maximum error={max(errors)}); not timed/ranked", flush=True)
        else:
            row["status"] = "OK"
            row["native_codec_ns"].append(metric(log, "codec_ns"))
            for _ in range(1, args.repetitions):
                repeated = run([executable.resolve(), streams[codec], "-", 4096])
                if f"hash={row['hash']} " not in repeated or "status=OK" not in repeated:
                    raise RuntimeError(f"Unstable repeat: {name}")
                row["native_codec_ns"].append(metric(repeated, "codec_ns"))
            row["native_fps"] = args.frames * 1e9 / sorted(row["native_codec_ns"])[len(row["native_codec_ns"]) // 2]
            print(f"{name}: native median {row['native_fps']:.2f} FPS, PSNR={psnr:.2f} dB", flush=True)
            if xe:
                row["xsim_image_sha256"] = hashlib.sha256(xe.read_bytes()).hexdigest()
                row["xsim_status"] = "pending"
                (work / "results.json").write_text(json.dumps(report, indent=2) + "\n")
                directory = work / name
                directory.mkdir(exist_ok=True)
                shutil.copyfile(streams[codec], directory / input_name)
                try:
                    simulated = run(["xsim", "--max-cycles", 600000000, "--xscope", "-offline trace.xmt",
                                     xe.resolve()], cwd=directory, timeout=args.sim_timeout)
                except subprocess.TimeoutExpired as error:
                    partial = error.stdout or b""
                    if isinstance(partial, bytes):
                        partial = partial.decode(errors="replace")
                    (directory / "xsim.log").write_text(partial)
                    row["xsim_status"] = "TIMEOUT"
                    row["xsim_timeout_seconds"] = args.sim_timeout
                    (work / "results.json").write_text(json.dumps(report, indent=2) + "\n")
                    raise SystemExit(f"FAIL: {name} exceeded {args.sim_timeout}s; no target FPS established. See {directory / 'xsim.log'}") from None
                (directory / "xsim.log").write_text(simulated)
                if f"Result: frames={args.frames} hash={row['hash']} " not in simulated or "status=OK" not in simulated:
                    raise RuntimeError(f"Incorrect xsim result: {name}\n{simulated}")
                row["xsim_codec_ns"] = metric(simulated, "codec_ns")
                row["xsim_fps"] = args.frames * 1e9 / row["xsim_codec_ns"]
                row["xsim_status"] = "OK"
                print(f"{name}: xsim {row['xsim_fps']:.2f} FPS", flush=True)
        (work / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Report: {work / 'results.json'}", flush=True)
    if failures:
        raise SystemExit(f"{failures} decoder(s) failed the independent correctness gate")


if __name__ == "__main__":
    main()
