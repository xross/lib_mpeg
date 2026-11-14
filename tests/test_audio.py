#!/usr/bin/env python3
# Copyright 2026 XMOS LIMITED.
# This Software is subject to the terms of the XMOS Public Licence: Version 1.
"""Self-contained elementary-MP2 precision, refill and replay checks."""
import argparse
from array import array
import math
from pathlib import Path
import subprocess


def run(command, *, expect=0):
    result = subprocess.run([str(item) for item in command], capture_output=True,
                            text=True, timeout=120)
    if result.returncode != expect:
        raise RuntimeError(f"{command}\n{result.stdout}{result.stderr}")
    return result.stdout


def compare_pcm(reference, actual):
    peak = noise = signal = 0.0
    samples = 0
    with reference.open("rb") as ref, actual.open("rb") as got:
        while True:
            a, b = ref.read(65536), got.read(65536)
            if len(a) != len(b):
                raise AssertionError("PCM sample count differs")
            if not a:
                break
            for x, y in zip(array("f", a), array("f", b)):
                if not math.isfinite(x) or not math.isfinite(y):
                    raise AssertionError("Non-finite PCM")
                delta = abs(x - y)
                peak = max(peak, delta)
                noise += delta * delta
                signal += x * x
                samples += 1
    if not samples:
        raise AssertionError("Empty PCM output")
    snr = 10 * math.log10(signal / noise) if noise else float("inf")
    if peak > 1e-6 or snr < 120:
        raise AssertionError(f"PCM precision regression: peak={peak}, SNR={snr}")
    return samples, peak, snr


def check(build, work, source, fragment):
    prefix = work / f"{source.stem}-{fragment}"
    pcm = prefix.with_suffix(".f32")
    ref, fast = prefix.with_suffix(".ref.f32"), prefix.with_suffix(".fast.f32")
    log = run([build / "mp2_bench", source, pcm, fragment])
    if log.count("PASS: audio play=") != 2:
        raise AssertionError("Repeated play did not finish")
    run([build / "mp2_upstream", source, ref])
    if pcm.read_bytes() != ref.read_bytes():
        raise AssertionError("Baseline PCM differs from pristine upstream")
    run([build / "mp2_bench_fast", source, fast, fragment])
    samples, peak, snr = compare_pcm(ref, fast)
    window = prefix.with_suffix(".window.f32")
    run([build / "mp2_bench_window", source, window, fragment])
    _, window_peak, window_snr = compare_pcm(ref, window)
    print(log, end="")
    print(f"PASS: fragment={fragment}, PCM samples={samples}, fast-scale peak={peak:.3g}, SNR={snr:.2f} dB")
    print(f"PASS: fused-window peak={window_peak:.3g}, SNR={window_snr:.2f} dB")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--full", type=Path)
    args = parser.parse_args()
    build, work = args.build.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    fixture = work / "short.mp2"
    run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
         "sine=frequency=997:sample_rate=48000", "-t", "0.72", "-c:a", "mp2",
         "-ar", "48000", "-ac", "2", "-b:a", "192k", "-f", "mp2", fixture])
    for fragment in (1, 7, 127, 4096):
        check(build, work, fixture, fragment)
    for rate, channels, bitrate in ((44100, 1, "64k"), (32000, 2, "128k")):
        other = work / f"audio-{rate}-{channels}.mp2"
        run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
             f"sine=frequency=997:sample_rate={rate}", "-t", "0.72", "-c:a", "mp2",
             "-ar", rate, "-ac", channels, "-b:a", bitrate, "-f", "mp2", other])
        check(build, work, other, 7)
    for name, data in (("empty", b""), ("bad-header", b"invalid MP2 stream"),
                       ("truncated", fixture.read_bytes()[:123])):
        bad = work / f"{name}.mp2"
        bad.write_bytes(data)
        for target in ("mp2_bench", "mp2_bench_fast", "mp2_bench_window"):
            run([build / target, bad, work / "bad.f32"], expect=1)
    if args.full:
        check(build, work, args.full.resolve(), 4096)
    print("PASS: elementary MP2 precision, refill boundaries, replay and failure cases")


if __name__ == "__main__":
    main()
