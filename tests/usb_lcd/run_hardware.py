#!/usr/bin/env python3
"""Bound a USB/LCD application run and validate its complete playback reports."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


STAGES = ("Load", "Wait for loader", "Feed", "Decode", "Transmit / ack wait",
          "Send YUV / backpressure", "Wait receiver ack", "Receive YUV / wait",
          "YUV -> RGB565", "Wait previous LCD", "LCD SPI")


class Reports:
    def __init__(self, frames, byte_count=None, timing=False, audio_blocks=None, sample_rate=48000,
                 audio_checksum=None):
        self.frames, self.byte_count, self.timing = frames, byte_count, timing
        self.audio_blocks, self.sample_rate = audio_blocks, sample_rate
        self.audio_checksum = audio_checksum
        self.plays = []
        self.current = {}

    def line(self, text):
        if (re.search(r"errors=[1-9]|live-after-close=[1-9]|\bET_", text)
                or "playback failed" in text):
            raise RuntimeError("Player reported an error: " + text)
        if text.startswith("MPEG pipeline drained:"):
            match = re.fullmatch(r"MPEG pipeline drained: decoded=(\d+) displayed=(\d+) "
                                 r"errors=0 status=2 bytes=(\d+)", text)
            if (not match or tuple(map(int, match.groups()[:2])) != (self.frames, self.frames)
                    or (self.byte_count is not None and int(match[3]) != self.byte_count)):
                raise RuntimeError("Incorrect frame count, EOF status or input byte count: " + text)
            self.current["drained"] = True
        elif text.startswith("MP2 decode/discard:"):
            match = re.fullmatch(r"MP2 decode/discard: bytes=(\d+) blocks=(\d+) samples/channel=(\d+) "
                                 r"rate=(\d+) errors=0 arena=(\d+) checksum=([0-9a-fA-F]+)", text)
            if not match:
                raise RuntimeError("Invalid audio report: " + text)
            if self.audio_blocks is not None and tuple(map(int, match.groups()[1:4])) != (
                    self.audio_blocks, self.audio_blocks * 1152, self.sample_rate):
                raise RuntimeError("Incorrect audio sample count or rate: " + text)
            if int(match[5]) > 28672:
                raise RuntimeError("Audio arena exceeds 28 KiB: " + text)
            if self.audio_checksum is not None and int(match[6], 16) != self.audio_checksum:
                raise RuntimeError("Incorrect audio PCM checksum: " + text)
            self.current["audio"] = {"bytes": int(match[1]), "blocks": int(match[2]),
                                     "samples": int(match[3]), "rate": int(match[4]),
                                     "arena_peak": int(match[5]), "checksum": match[6]}
        elif text.startswith("MPEG arena:"):
            match = re.fullmatch(r"MPEG arena: peak=(\d+) live-after-close=0 bytes", text)
            if not match:
                raise RuntimeError("Invalid arena accounting: " + text)
            self.current["arena_peak"] = int(match[1])
        elif text.startswith("MPEG timing "):
            match = re.fullmatch(r"MPEG timing (decode|service): count=(\d+) min=(\d+) "
                                 r"p50_le=(\d+) p95_le=(\d+) p99_le=(\d+) max=(\d+) over_40ms=(\d+)", text)
            if not match or int(match[2]) != self.frames - (match[1] == "service"):
                raise RuntimeError("Invalid timing distribution: " + text)
            names = ("count", "min_us", "p50_upper_us", "p95_upper_us", "p99_upper_us", "max_us", "over_40ms")
            self.current.setdefault("timing", {})[match[1]] = dict(zip(names, map(int, match.groups()[1:])))
        elif text.startswith("Play ") and " complete:" in text:
            match = re.fullmatch(r"Play (\d+) complete: (\d+) frames in ([\d.]+) seconds: ([\d.]+) FPS", text)
            if not match or int(match[1]) != len(self.plays) + 1 or int(match[2]) != self.frames:
                raise RuntimeError("Invalid completed-play report: " + text)
            self.current.update(number=int(match[1]), seconds=float(match[3]), fps=float(match[4]), stages_us={})
        elif text.startswith("Startup to first displayed frame:"):
            match = re.search(r"(\d+) us$", text)
            if not match:
                raise RuntimeError("Invalid startup timing: " + text)
            self.current["startup_us"] = int(match[1])
        else:
            match = re.fullmatch(r"\s+(.+?)\s+(1 -> 0|0|1)\s+(\d+)", text)
            if match and match[1].strip() in STAGES:
                if "stages_us" not in self.current:
                    raise RuntimeError("Stage report without completed play")
                self.current["stages_us"][match[1].strip()] = int(match[3])
                if match[1].strip() == "LCD SPI":
                    required = ("drained", "arena_peak", "number", "startup_us")
                    if not all(key in self.current for key in required) or set(self.current["stages_us"]) != set(STAGES):
                        raise RuntimeError("Incomplete completed-play report")
                    if self.timing and set(self.current.get("timing", {})) != {"decode", "service"}:
                        raise RuntimeError("Missing frame timing distributions")
                    if self.audio_blocks is not None and "audio" not in self.current:
                        raise RuntimeError("Missing audio decoder statistics")
                    self.plays.append(self.current)
                    self.current = {}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def measure(executable, adapter, plays, timeout, reports, log_path):
    import Pyxrun

    def completed(text):
        print(text, flush=True)
        reports.line(text)
        return len(reports.plays) >= plays

    if not Pyxrun.run(executable, adapter_id=adapter, timeout=timeout, until=completed,
                      xscope_io_only=True, log_path=log_path, echo=False):
        raise RuntimeError(f"Player hardware run failed; see {log_path}")
    return {"xe_sha256": digest(executable), "plays": reports.plays}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xe", type=Path)
    parser.add_argument("--adapter-id", "--adapter", required=True)
    parser.add_argument("--stream", type=Path, help="Local VIDEO.M1V counterpart for byte-count checks (not on-device hashing)")
    parser.add_argument("--frames", type=int, default=3025)
    parser.add_argument("--plays", type=int, default=2)
    parser.add_argument("--timeout", type=float, default=360)
    parser.add_argument("--timing", action="store_true", help="Require optional decode/service distributions")
    parser.add_argument("--audio", action="store_true", help="Require decoded MP2 sample/block statistics")
    parser.add_argument("--audio-blocks", type=int, default=5042)
    parser.add_argument("--sample-rate", type=int, default=48000)
    parser.add_argument("--audio-checksum", type=lambda text: int(text, 16),
                        help="Expected PCM checksum in hex; use the selected format's reference")
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    if min(args.frames, args.plays, args.audio_blocks, args.sample_rate) < 1 or args.timeout <= 0:
        parser.error("counts, sample rate and timeout must be positive")
    for path in (args.xe, args.stream):
        if path is None:
            continue
        if not path.is_file():
            parser.error(f"File not found: {path}")
    args.work.mkdir(parents=True, exist_ok=True)
    result = {"adapter": args.adapter_id, "frames": args.frames, "timing_instrumented": args.timing,
              "xtc": subprocess.check_output(["xcc", "--version"], text=True)}
    byte_count = None
    if args.stream:
        byte_count = result["stream_bytes"] = args.stream.stat().st_size
        result["stream_sha256"] = digest(args.stream)
    reports = Reports(args.frames, byte_count, args.timing,
                      args.audio_blocks if args.audio else None, args.sample_rate, args.audio_checksum)
    result["run"] = measure(args.xe, args.adapter_id, args.plays, args.timeout, reports, args.work / "xrun.log")
    (args.work / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"PASS: {args.plays} complete hardware plays, {args.frames} frames/play")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error)) from error
