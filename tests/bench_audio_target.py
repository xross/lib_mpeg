#!/usr/bin/env python3
"""Four-way target timing/PCM check on exactly the same RAM-loaded MP2 frames.

Source the XTC environment before running. One idle application logical core on a
600-MHz target; hardware timer ticks are 10 ns. The fixture is embedded at build
time; refill, checksums and xSCOPE dumps are outside timed mpeg_audio_next() calls.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import subprocess
from test_audio import compare_pcm

VARIANTS = ("float_original", "float_fused", "s32_scalar", "s32_vector")


def run(command, cwd=None):
    result = subprocess.run([str(x) for x in command], cwd=cwd, text=True,
                            capture_output=True, timeout=180)
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout}{result.stderr}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--source", type=Path, help="MP2 elementary stream; default generates stereo multitone")
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--start", type=float, default=0, help="Start time in source, seconds")
    board = parser.add_mutually_exclusive_group(required=True)
    board.add_argument("--adapter", help="XTAG serial")
    board.add_argument("--sim", action="store_true")
    args = parser.parse_args()
    if args.frames < 1 or args.start < 0:
        parser.error("frames must be positive and start must be nonnegative")
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    fixture = work / "audio-bench.mp2"
    source = args.source.resolve() if args.source else work / "multitone.mp2"
    if not args.source:
        run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
             "aevalsrc=0.3*sin(2*PI*997*t)+0.2*sin(2*PI*71*t)|0.3*sin(2*PI*3001*t)+0.2*sin(2*PI*173*t):s=48000:d=1",
             "-c:a", "mp2", "-b:a", "192k", "-f", "mp2", source])
    # ffmpeg copies whole compressed packets, so no partial frame at the end.
    run(["ffmpeg", "-v", "error", "-y", "-ss", args.start, "-i", source, "-frames:a", args.frames,
         "-c:a", "copy", "-f", "mp2", fixture])
    if not 0 < fixture.stat().st_size <= 32768:
        raise ValueError("Fixture must fit the 32768-byte RAM staging buffer")
    build = args.build.resolve()
    generated = build / "fixture" / "audio_bench_input.h"
    generated.parent.mkdir(parents=True, exist_ok=True)
    data = fixture.read_bytes()
    lines = ["/* Generated benchmark fixture; not decoder memory. */",
             "static const unsigned char input[] = {"]
    for i in range(0, len(data), 16):
        lines.append(",".join(str(x) for x in data[i:i+16]) + ",")
    generated.write_text("\n".join(lines) + "\n};\n")
    (work / "build.log").write_text(run(["xmake", "-C", build, "-j", "8"]))
    reference = work / "upstream.f32"
    run([args.native.resolve() / "mp2_upstream", fixture, reference])
    # The native upstream test deliberately emits two plays to check replay.
    pcm = reference.read_bytes()
    assert len(pcm) == args.frames * 1152 * 2 * 4 * 2
    assert pcm[:len(pcm)//2] == pcm[len(pcm)//2:]
    reference.write_bytes(pcm[:len(pcm)//2])
    integer = None
    report = {"input_sha256": hashlib.sha256(data).hexdigest(), "input_bytes": len(data),
              "xtc": run(["xcc", "--version"]), "transport": "xsim" if args.sim else "xscope",
              "adapter": args.adapter, "source_start_seconds": args.start,
              "variants": {}}
    print("variant,blocks,us/frame,min-us,max-us,arena,checksum,peak-error,SNR-dB", flush=True)
    expected_blocks = None
    for variant in VARIANTS:
        xe = args.build.resolve() / "bin" / variant / f"test_mp2_bench_{variant}.xe"
        # Retain each attempt when captured PCM fails the target's checksum.
        # The corruption cause remains unverified; retries do not establish reliability.
        for attempt in range(3):
            attempt_log = work / f"{variant}.attempt-{attempt + 1}.log"
            if args.sim:
                log = run(["xsim", "--max-cycles", "500000000", "--xscope",
                           f"-offline {work / (variant + '.vcd')}", xe], cwd=work)
                attempt_log.write_text(log)
            else:
                import Pyxrun

                if not Pyxrun.run(xe, adapter_id=args.adapter, timeout=180, cwd=work,
                                  xscope_io_only=True, log_path=attempt_log, echo=False):
                    raise RuntimeError(f"Benchmark hardware run failed; see {attempt_log}")
                log = attempt_log.read_text(errors="replace")
            (work / f"{variant}.log").write_text(log)
            match = re.search(r"PASS: blocks=(\d+) rate=(\d+) ticks=(\d+) min=(\d+) max=(\d+) arena=(\d+) checksum=(\w+) pcm=(\w+)", log)
            if not match:
                raise AssertionError(log)
            blocks, rate, ticks, least, most, arena = map(int, match.groups()[:6])
            if expected_blocks is None:
                expected_blocks = blocks
            assert blocks == expected_blocks == args.frames
            actual = work / f"{variant}.pcm"
            actual.write_bytes(b"".join(bytes.fromhex(x) for x in re.findall(r"^PCM ([0-9a-f]+)$", log, re.M)))
            pcm_hash = 2166136261
            for byte in actual.read_bytes():
                pcm_hash = ((pcm_hash ^ byte) * 16777619) & 0xffffffff
            if pcm_hash == int(match[8], 16):
                break
            print(f"{variant}: PCM transport checksum mismatch, attempt {attempt + 1}", file=sys.stderr, flush=True)
        else:
            raise AssertionError("PCM transport checksum mismatch")
        s32 = variant.startswith("s32")
        _, peak, snr = compare_pcm(reference, actual, s32=s32)
        if variant == "s32_scalar":
            integer = actual.read_bytes()
        if variant == "s32_vector":
            assert actual.read_bytes() == integer, "Scalar/vector PCM differs"
        print(f"{variant},{blocks},{ticks/blocks/100:.2f},{least/100:.2f},{most/100:.2f},{arena},{match[7]},{peak:.3g},{snr:.2f}", flush=True)
        sections = run(["xobjdump", "--size", xe])
        (work / f"{variant}.size").write_text(sections)
        resources = run(["xobjdump", "--resources", xe])
        (work / f"{variant}.resources").write_text(resources)
        memory = re.search(r"Loadable 2 for tile\[0\].*?text\s+data\s+bss\s+total\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", sections, re.S)
        stack = re.search(r"tile\[0\].*stack usage,\s+upper bound: (\d+)", resources)
        report["variants"][variant] = {"blocks": blocks, "rate": rate, "ticks": ticks,
            "attempts": attempt + 1,
            "min_ticks": least, "max_ticks": most, "arena_bytes": arena,
            "peak_error": peak, "snr_db": snr if snr != float("inf") else None,
            "checksum": match[7], "pcm_fnv1a": match[8],
            "sections": dict(zip(("text", "data", "bss", "total"), map(int, memory.groups()))),
            "stack_reservation_bytes": int(stack[1]),
            "xe_sha256": hashlib.sha256(xe.read_bytes()).hexdigest()}
        (work / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: four-way precision, frame counts, scalar/vector PCM identity and guarded arena")


if __name__ == "__main__":
    main()
