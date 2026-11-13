#!/usr/bin/env python3
# Copyright 2026 XMOS LIMITED.
# This Software is subject to the terms of the XMOS Public Licence: Version 1.
"""Bounded, checked smoke run; activate XTC Tools before invoking."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--xe", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument("--hash", required=True)
    parser.add_argument("--cycles", type=int, default=600000000)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    command = ["xsim", "--max-cycles", str(args.cycles), "--xscope", "-offline plmpeg.xmt", str(args.xe.resolve())]
    try:
        result = subprocess.run(command, cwd=args.work.resolve(), capture_output=True, text=True, timeout=args.timeout)
    except subprocess.TimeoutExpired as error:
        raise SystemExit(f"FAIL: xsim did not complete within {args.timeout}s: {error.stdout!r}") from error
    log = result.stdout + result.stderr
    (args.work / "xsim.log").write_text(log)
    print(log, end="")
    marker = f"Result: frames={args.frames} hash={args.hash} "
    if result.returncode or marker not in log or "status=OK" not in log:
        raise SystemExit("FAIL: xsim did not return the expected completed decode/hash")


if __name__ == "__main__":
    main()
