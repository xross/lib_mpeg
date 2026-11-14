#!/usr/bin/env python3
# Copyright 2026 XMOS LIMITED.
# This Software is subject to the terms of the XMOS Public Licence: Version 1.
"""Compare two IDCT builds using the shared USB/LCD hardware runner."""
import argparse
import json
from pathlib import Path
import subprocess
from run_hardware import Reports, digest, measure


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--vpu", type=Path, required=True)
    parser.add_argument("--adapter-id", required=True)
    parser.add_argument("--stream", type=Path, required=True, help="Local counterpart of USB VIDEO.M1V")
    parser.add_argument("--frames", type=int, default=3025)
    parser.add_argument("--plays", type=int, default=2)
    parser.add_argument("--timeout", type=float, default=360)
    parser.add_argument("--timing", action="store_true")
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    if args.frames < 1 or args.plays < 2 or args.timeout <= 0:
        parser.error("frames and timeout must be positive; at least two plays are required")
    for path in (args.original, args.vpu, args.stream):
        if not path.is_file():
            parser.error(f"File not found: {path}")
    args.work.mkdir(parents=True, exist_ok=True)
    result = {"adapter": args.adapter_id, "frames": args.frames,
              "stream_bytes": args.stream.stat().st_size, "stream_sha256": digest(args.stream),
              "timing_instrumented": args.timing,
              "xtc": subprocess.check_output(["xcc", "--version"], text=True)}
    for name, executable in (("original", args.original), ("vpu", args.vpu)):
        reports = Reports(args.frames, result["stream_bytes"], args.timing)
        result[name] = measure(executable, args.adapter_id, args.plays, args.timeout, reports, args.work / f"{name}.log")
        (args.work / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    # Exclude startup; use durations, not averages of rounded console FPS.
    rates = {name: args.frames * (args.plays - 1) / sum(p["seconds"] for p in result[name]["plays"][1:])
             for name in ("original", "vpu")}
    result["steady_fps"] = rates
    result["fps_gain_percent"] = (rates["vpu"] / rates["original"] - 1) * 100
    (args.work / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"PASS: original={rates['original']:.3f} FPS vpu={rates['vpu']:.3f} FPS gain={result['fps_gain_percent']:.2f}%")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error)) from error
