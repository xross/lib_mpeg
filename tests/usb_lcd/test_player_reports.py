#!/usr/bin/env python3
"""Host-only checks of player reports and their completion predicate."""
import unittest
from contextlib import redirect_stdout
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch
from run_hardware import Reports, STAGES, measure


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
        self.assertEqual(parser.current["audio"]["checksum"], "abcdef12")
        Reports(6, audio_checksum=0xabcdef12).line(audio)
        with self.assertRaises(RuntimeError):
            Reports(6, audio_checksum=0x12345678).line(audio)
        with self.assertRaises(RuntimeError):
            Reports(6).line(audio.replace("arena=20000", "arena=28673"))
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


class RunnerTests(unittest.TestCase):
    def test_reports_stop_the_player_after_the_required_plays(self):
        with tempfile.TemporaryDirectory() as directory:
            xe = Path(directory) / "player.xe"
            xe.write_bytes(b"fixture")
            log = Path(directory) / "player.log"
            reports = Reports(6, 1234)

            def run(executable, **options):
                self.assertEqual(executable, xe)
                self.assertEqual(options["adapter_id"], "SERIAL")
                self.assertEqual(options["timeout"], 30)
                self.assertEqual(options["log_path"], log)
                self.assertTrue(options["xscope_io_only"])
                self.assertFalse(options["echo"])
                for number in (1, 2, 3):
                    for line in report(number=number):
                        if options["until"](line):
                            return True
                return False

            with patch.dict(sys.modules, {"Pyxrun": SimpleNamespace(run=run)}), \
                    redirect_stdout(io.StringIO()):
                result = measure(xe, "SERIAL", 2, 30, reports, log)
            self.assertEqual(len(result["plays"]), 2)

            with patch.dict(sys.modules, {"Pyxrun": SimpleNamespace(run=lambda *a, **k: False)}):
                with self.assertRaisesRegex(RuntimeError, "Player hardware run failed"):
                    measure(xe, "SERIAL", 2, 30, Reports(6), log)


if __name__ == "__main__":
    unittest.main()
