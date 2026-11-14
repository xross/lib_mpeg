#!/usr/bin/env python3
# Copyright 2026 XMOS LIMITED.
# This Software is subject to the terms of the XMOS Public Licence: Version 1.
import unittest
from run_hardware import Reports, STAGES


def report(frames=6, byte_count=1234, number=1):
    return [f"MPEG pipeline drained: decoded={frames} displayed={frames} errors=0 status=2 bytes={byte_count}",
            "MPEG arena: peak=379288 live-after-close=0 bytes",
            f"Play {number} complete: {frames} frames in 0.250 seconds: 24.00 FPS",
            "Startup to first displayed frame: 123 us"] + [f"  {stage:24} 1 100" for stage in STAGES]


class ParserTests(unittest.TestCase):
    def test_complete_reports_and_reset(self):
        parser = Reports(6, 1234)
        for line in report()[:-1]:
            parser.line(line)
        self.assertEqual(parser.plays, [])  # FPS alone is not a completed report.
        parser.line(report()[-1])
        for line in report(number=2):
            parser.line(line)
        self.assertEqual(len(parser.plays), 2)
        self.assertEqual(parser.plays[1]["stages_us"]["Decode"], 100)

    def test_failures(self):
        for line in (report(frames=5)[0], report(byte_count=12)[0], report(number=2)[2],
                     "MPEG pipeline drained: decoded=6 displayed=6 errors=1 status=2 bytes=1234",
                     "MPEG arena: peak=379288 live-after-close=4 bytes", "Program received signal ET_ECALL"):
            with self.subTest(line=line), self.assertRaises(RuntimeError):
                Reports(6, 1234).line(line)

    def test_incomplete_and_missing_timing(self):
        for timing in (False, True):
            parser = Reports(6, 1234, timing)
            lines = report() if timing else report()[1:]
            with self.assertRaises(RuntimeError):
                for line in lines:
                    parser.line(line)

    def test_timing(self):
        parser = Reports(6, 1234, True)
        for name in ("decode", "service"):
            count = 6 if name == "decode" else 5
            parser.line(f"MPEG timing {name}: count={count} min=100 p50_le=1000 p95_le=2000 p99_le=2000 max=1500 over_40ms=0")
        for line in report():
            parser.line(line)
        self.assertEqual(parser.plays[0]["timing"]["decode"]["p95_upper_us"], 2000)

    def test_audio_counts_and_required_report(self):
        audio = "MP2 decode/discard: bytes=100 blocks=2 samples/channel=2304 rate=44100 errors=0 arena=20000 checksum=abcdef12"
        parser = Reports(6, audio_blocks=2, sample_rate=44100)
        parser.line(audio)
        for line in report():
            parser.line(line)
        self.assertEqual(parser.plays[0]["audio"]["samples"], 2304)
        with self.assertRaises(RuntimeError):
            Reports(6, audio_blocks=3, sample_rate=44100).line(audio)
        parser = Reports(6, audio_blocks=2)
        with self.assertRaises(RuntimeError):
            for line in report():
                parser.line(line)

    def test_optional_byte_count(self):
        parser = Reports(6)
        for line in report(byte_count=9999):
            parser.line(line)
        self.assertEqual(len(parser.plays), 1)


if __name__ == "__main__":
    unittest.main()
