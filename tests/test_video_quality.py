#!/usr/bin/env python3
# Copyright 2026 XMOS LIMITED.
# This Software is subject to the terms of the XMOS Public Licence: Version 1.
"""Full-sequence video comparisons, with an independent FFmpeg decode."""
import argparse
from pathlib import Path
import re
import subprocess


def run(*command):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout}{result.stderr}")
    return result.stdout


def check(build, work, source):
    scalar = work / f"{source.stem}.decoded.yuv"
    upstream = work / f"{source.stem}.upstream.yuv"
    independent = work / f"{source.stem}.ffmpeg.yuv"
    print(run(build / "mpeg_bench", source, scalar), end="")
    print(run(build / "plmpeg_upstream_ref", source, upstream, "--drain"), end="")
    run("ffmpeg", "-v", "error", "-y", "-i", source, "-map", "0:v:0", "-pix_fmt", "yuv420p",
        "-fps_mode", "passthrough", "-f", "rawvideo", independent)
    for ref, actual in ((upstream, scalar), (independent, upstream), (independent, scalar)):
        metrics = run(build / "yuv_compare", ref, actual, 320, 240)
        print(f"{actual.name} against {ref.name}:\n{metrics}", end="")
        for line in metrics.splitlines()[1:]:
            peak = int(re.search(r"peak=(\d+)", line)[1])
            mse = float(re.search(r"mse=([\d.]+)", line)[1])
            if peak > 16 or mse > 2:
                raise AssertionError("Video error exceeds this regression fixture's bound")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--full", type=Path)
    args = parser.parse_args()
    build, work = args.build.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    for name, source in (("pattern", "testsrc2=size=320x240:rate=25"),
                         ("dc", "color=c=0x6789ab:size=320x240:rate=25")):
        fixture = work / f"{name}.m1v"
        run("ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", source, "-t", "0.72",
            "-c:v", "mpeg1video", "-g", "12", "-bf", "2", "-q:v", "6", "-threads:v", "1",
            "-f", "mpeg1video", fixture)
        check(build, work, fixture)
    if args.full:
        check(build, work, args.full.resolve())
    print("PASS: video frame counts, planar accuracy, predicted sequence and independent decoder")


if __name__ == "__main__":
    main()
