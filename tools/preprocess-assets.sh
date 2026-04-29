#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
source_dir="$project_dir/assets/source"
processed_dir="$project_dir/assets/processed"

fps=30
canvas_width=1920
canvas_height=1080
background_opacity=0.38

require_file() {
  local path=$1
  if [[ ! -f "$path" ]]; then
    printf 'missing required file: %s\n' "$path" >&2
    exit 1
  fi
}

require_command() {
  local command_name=$1
  if ! command -v "$command_name" >/dev/null 2>&1; then
    printf 'missing required command: %s\n' "$command_name" >&2
    exit 1
  fi
}

stream_value() {
  local path=$1
  local entry=$2
  ffprobe -v error -select_streams v:0 \
    -show_entries "stream=${entry}" \
    -of default=noprint_wrappers=1:nokey=1 "$path"
}

stream_tag_value() {
  local path=$1
  local tag=$2
  ffprobe -v error -select_streams v:0 \
    -show_entries "stream_tags=${tag}" \
    -of default=noprint_wrappers=1:nokey=1 "$path"
}

count_video_frames() {
  local path=$1
  ffprobe -v error -count_frames -select_streams v:0 \
    -show_entries stream=nb_read_frames \
    -of default=noprint_wrappers=1:nokey=1 "$path"
}

validate_alpha_webm() {
  local path=$1
  local width height rate alpha_mode

  width=$(stream_value "$path" width)
  height=$(stream_value "$path" height)
  rate=$(stream_value "$path" avg_frame_rate)
  alpha_mode=$(stream_tag_value "$path" alpha_mode)

  if [[ "$width" != "$canvas_width" || "$height" != "$canvas_height" ]]; then
    printf '%s must be %sx%s, got %sx%s\n' "$path" "$canvas_width" "$canvas_height" "$width" "$height" >&2
    exit 1
  fi
  if [[ "$rate" != "$fps/1" ]]; then
    printf '%s must be %s fps, got %s\n' "$path" "$fps" "$rate" >&2
    exit 1
  fi
  if [[ "$alpha_mode" != "1" ]]; then
    printf '%s must be VP9 WebM with alpha_mode=1\n' "$path" >&2
    exit 1
  fi
}

require_command ffprobe
require_file "$source_dir/neko1.webm"
require_file "$source_dir/neko2.webm"

validate_alpha_webm "$source_dir/neko1.webm"
validate_alpha_webm "$source_dir/neko2.webm"

rm -rf "$processed_dir"
mkdir -p "$processed_dir"

clip1_frames=$(count_video_frames "$source_dir/neko1.webm")
clip2_frames=$(count_video_frames "$source_dir/neko2.webm")

if [[ "$clip1_frames" -le 0 || "$clip2_frames" -le 0 ]]; then
  printf 'ffprobe did not count both videos\n' >&2
  exit 1
fi

cat >"$processed_dir/manifest.conf" <<EOF
version=3
asset_format=video_alpha
fps=${fps}
width=${canvas_width}
height=${canvas_height}
global_opacity=1.0
background_opacity=${background_opacity}
clip1_name=neko1
clip1_video=neko1.webm
clip1_frames=${clip1_frames}
clip1_loop=false
clip1_frame_width=${canvas_width}
clip1_frame_height=${canvas_height}
clip1_offset_x=0
clip1_offset_y=0
clip2_name=neko2
clip2_video=neko2.webm
clip2_frames=${clip2_frames}
clip2_loop=true
clip2_frame_width=${canvas_width}
clip2_frame_height=${canvas_height}
clip2_offset_x=0
clip2_offset_y=0
EOF

printf 'Generated manifest for alpha WebM assets: neko1=%s frames, neko2=%s frames.\n' "$clip1_frames" "$clip2_frames"
