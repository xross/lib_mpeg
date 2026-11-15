#!/usr/bin/env python3
"""Check PCM retry artifacts and benchmark reports without hardware or XTC Tools."""
from contextlib import redirect_stdout, redirect_stderr
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import bench_audio_target as bench


class BenchmarkReportTests(unittest.TestCase):
    def test_checksum_retry_preserves_attempts_and_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            build, work = root / "build", root / "work"
            pcm = bytes(1152 * 2 * 4)
            checksum = 2166136261
            for byte in pcm:
                checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
            for variant in bench.VARIANTS:
                xe = build / "bin" / variant / f"test_mp2_bench_{variant}.xe"
                xe.parent.mkdir(parents=True)
                xe.write_bytes(b"XE")

            def host(command, cwd=None):
                command = [str(arg) for arg in command]
                if command[0] == "ffmpeg":
                    Path(command[-1]).write_bytes(b"MP2 fixture")
                elif command[0].endswith("mp2_upstream"):
                    Path(command[-1]).write_bytes(pcm * 2)
                elif command[0] == "xobjdump":
                    return ("Loadable 2 for tile[0]\ntext data bss total\n1 2 3 6\n"
                            if command[1] == "--size" else "tile[0] stack usage, upper bound: 4096\n")
                return "host command completed\n"

            calls = []

            def hardware(xe, **options):
                calls.append(options)
                self.assertEqual(options["adapter_id"], "SERIAL")
                self.assertEqual(options["timeout"], 180)
                self.assertEqual(options["cwd"], work)
                self.assertTrue(options["xscope_io_only"])
                self.assertFalse(options["echo"])
                # First capture is corrupt; the second must not overwrite its log.
                transferred = b"bad" if len(calls) == 1 else pcm
                options["log_path"].write_text(
                    f"PCM {transferred.hex()}\nPASS: blocks=1 rate=48000 ticks=100 min=100 "
                    f"max=100 arena=27808 checksum=00000000 pcm={checksum:08x}\n")
                return True

            args = ["bench_audio_target.py", "--build", str(build), "--native", str(root),
                    "--work", str(work), "--frames", "1", "--adapter", "SERIAL"]
            with patch.object(sys, "argv", args), patch.object(bench, "run", host), \
                    patch.object(bench, "compare_pcm", return_value=(len(pcm) // 4, 0, float("inf"))), \
                    patch.dict(sys.modules, {"Pyxrun": SimpleNamespace(run=hardware)}), \
                    redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                bench.main()
            self.assertEqual(len(calls), 5)
            first = work / "float_original.attempt-1.log"
            second = work / "float_original.attempt-2.log"
            self.assertNotEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(second.read_bytes(), (work / "float_original.log").read_bytes())
            result = json.loads((work / "report.json").read_text())
            self.assertEqual(result["variants"]["float_original"]["attempts"], 2)
            self.assertEqual(result["adapter"], "SERIAL")


if __name__ == "__main__":
    unittest.main()
