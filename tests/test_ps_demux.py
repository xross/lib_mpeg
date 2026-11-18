#!/usr/bin/env python3
"""Compare lib_mpeg's storage-independent PS reader bytes with FFmpeg."""
import argparse
from pathlib import Path
import subprocess


def run(command):
    subprocess.run([str(value) for value in command], check=True, timeout=30,
                   capture_output=True)


def check(ffmpeg, demux, work, source, has_audio=True):
    video_ref, audio_ref = work / "reference.m1v", work / "reference.mp2"
    run([ffmpeg, "-v", "error", "-y", "-i", source, "-map", "0:v:0", "-c", "copy",
         "-f", "mpeg1video", video_ref])
    if has_audio:
        run([ffmpeg, "-v", "error", "-y", "-i", source, "-map", "0:a:0", "-c", "copy",
             "-f", "mp2", audio_ref])
    else:
        audio_ref.write_bytes(b"")
    expected = video_ref.read_bytes(), audio_ref.read_bytes()
    assert expected[0] and (expected[1] or not has_audio)
    for fragment, capacity in ((1, 1), (7, 127), (4095, 4096), (4096, 4096)):
        video, audio = work / "actual.m1v", work / "actual.mp2"
        run([demux, source, video, audio, fragment, capacity])
        assert (video.read_bytes(), audio.read_bytes()) == expected, (source, fragment, capacity)
    print(f"PASS: {source.name}, video={len(expected[0])}, audio={len(expected[1])}; four refill/output boundaries")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demux", type=Path, required=True)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--full", type=Path, help="Optional MPEG-1 PS with video and MP2 audio")
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    for name, bframes, audio in (("ipb", 2, True), ("ip", 0, True), ("video-only", 2, False)):
        source = work / f"{name}.mpg"
        command = [args.ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
                   "testsrc2=size=320x240:rate=25"]
        if audio:
            command += ["-f", "lavfi", "-i", "sine=frequency=997:sample_rate=48000"]
        command += ["-t", "0.48", "-c:v", "mpeg1video", "-g", "6", "-bf", str(bframes),
                    "-q:v", "6", "-pix_fmt", "yuv420p"]
        if audio:
            command += ["-c:a", "mp2", "-ar", "48000", "-ac", "2", "-b:a", "192k"]
        command += ["-f", "mpeg", source]
        run(command)
        check(args.ffmpeg, args.demux.resolve(), work, source, audio)
    if args.full:
        check(args.ffmpeg, args.demux.resolve(), work, args.full.resolve())


if __name__ == "__main__":
    main()
