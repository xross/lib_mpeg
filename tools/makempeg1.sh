#!/usr/bin/env bash
# Copyright 2026 XMOS LIMITED.
# This Software is subject to the terms of the XMOS Public Licence: Version 1.
set -euo pipefail

if [[ $# -ne 3 ]]; then
    printf 'Usage: bash %s source.mp4 crysis_meme.jpg VIDEO.M1V\n' "$0" >&2
    printf 'VIDEO_CROP overrides the source-specific letterbox crop.\n' >&2
    exit 2
fi
source_video=$1
intro_image=$2
output=$3
[[ -f $source_video && -f $intro_image ]] || { printf 'Input media not found.\n' >&2; exit 1; }
[[ ! -e $output ]] || { printf 'Refusing to overwrite %s\n' "$output" >&2; exit 1; }

# Match the MJPEG intro: remove its letterboxing, retaining the largest detected
# active picture so that darker regions do not cause an over-aggressive crop.
log=$(ffmpeg -hide_banner -loop 1 -framerate 25 -t 1 -i "$intro_image" \
    -vf 'cropdetect=24:2:0' -an -f null - 2>&1) || { printf '%s\n' "$log" >&2; exit 1; }
intro_crop=''
best_area=0
while IFS= read -r line; do
    if [[ $line =~ crop=([0-9]+):([0-9]+):([0-9]+):([0-9]+) ]]; then
        area=$((BASH_REMATCH[1] * BASH_REMATCH[2]))
        if (( area > best_area )); then
            best_area=$area
            intro_crop=${BASH_REMATCH[0]}
        fi
    fi
done <<< "$log"
[[ -n $intro_crop ]] || { printf 'No active intro picture detected.\n' >&2; exit 1; }

# The reference source is 3840x2160 with approximately 260-pixel top/bottom bars.
# Preserve its complete width. For unletterboxed sources use
# VIDEO_CROP=crop=iw:ih:0:0 rather than the source-specific default below.
video_crop=${VIDEO_CROP:-crop=iw:ih-520:0:260}
printf 'Intro: %s\nVideo: %s\n' "$intro_crop" "$video_crop"

ffmpeg -hide_banner -n -filter_complex_threads 1 \
    -loop 1 -framerate 25 -t 3 -i "$intro_image" \
    -ss 7 -i "$source_video" \
    -filter_complex \
    "[0:v]${intro_crop},scale=320:240:flags=lanczos,setsar=1,fps=25,format=yuv420p,setpts=PTS-STARTPTS[intro]; \
     [1:v]${video_crop},scale=320:240:flags=lanczos,setsar=1,fps=25,format=yuv420p,setpts=PTS-STARTPTS[video]; \
     [intro][video]concat=n=2:v=1:a=0[out]" \
    -map '[out]' -an -c:v mpeg1video -threads:v 1 \
    -g 12 -bf 2 -q:v 6 -sc_threshold 1000000000 -f mpeg1video "$output"
