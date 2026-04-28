#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
source_dir="$project_dir/assets/source"
processed_dir="$project_dir/assets/processed"

fps=30
canvas_width=1920
canvas_height=1080
matte_threshold=32
matte_feather=0.7
background_opacity=0.38
intro_slide_seconds=3

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

count_video_frames() {
  local path=$1
  ffprobe -v error -count_frames -select_streams v:0 \
    -show_entries stream=nb_read_frames \
    -of default=noprint_wrappers=1:nokey=1 "$path"
}

outline_matte_filter() {
  local right=$((canvas_width - 1))
  local bottom=$((canvas_height - 1))
  printf '[0:v]fps=%s,format=rgba,split[base][masksrc];' "$fps"
  printf '[masksrc]format=gray,lut='"'"'if(lte(val,%s),0,255)'"'"',' "$matte_threshold"
  printf 'floodfill=x=0:y=0:s0=0:d0=128,'
  printf 'floodfill=x=%s:y=0:s0=0:d0=128,' "$right"
  printf 'floodfill=x=0:y=%s:s0=0:d0=128,' "$bottom"
  printf 'floodfill=x=%s:y=%s:s0=0:d0=128,' "$right" "$bottom"
  printf 'lut='"'"'if(eq(val,128),0,255)'"'"',gblur=sigma=%s[alpha];' "$matte_feather"
  printf '[base][alpha]alphamerge,format=rgba'
}

side_by_side_filter() {
  local mode=$1
  printf '%s' "$(outline_matte_filter)"
  if [[ "$mode" == "slide-in" ]]; then
    printf '[matte];'
    printf 'color=c=black@0:s=%sx%s:r=%s,format=rgba[blank];' "$canvas_width" "$canvas_height" "$fps"
    printf '[blank][matte]overlay=x='"'"'if(lt(t\\,%s)\\,%s*(1-t/%s)\\,0)'"'"':y=0:shortest=1:format=auto,format=rgba,' \
      "$intro_slide_seconds" "$canvas_width" "$intro_slide_seconds"
  else
    printf ','
  fi
  printf 'split[color_src][alpha_src];'
  printf '[color_src]format=rgb24[color];'
  printf '[alpha_src]alphaextract,format=gray,format=rgb24[alpha];'
  printf '[color][alpha]hstack=inputs=2,format=yuv420p'
}

require_command ffmpeg
require_command ffprobe
require_command du
require_file "$source_dir/neko1.webm"
require_file "$source_dir/neko2.webm"

rm -rf "$processed_dir"
mkdir -p "$processed_dir"

ffmpeg -y -hide_banner -loglevel error -i "$source_dir/neko1.webm" -an \
  -filter_complex "$(side_by_side_filter slide-in)" \
  -c:v libvpx-vp9 -pix_fmt yuv420p -crf 22 -b:v 0 -deadline good -row-mt 1 \
  "$processed_dir/neko1.webm"

ffmpeg -y -hide_banner -loglevel error -i "$source_dir/neko2.webm" -an \
  -filter_complex "$(side_by_side_filter static)" \
  -c:v libvpx-vp9 -pix_fmt yuv420p -crf 22 -b:v 0 -deadline good -row-mt 1 \
  "$processed_dir/neko2.webm"

clip1_frames=$(count_video_frames "$processed_dir/neko1.webm")
clip2_frames=$(count_video_frames "$processed_dir/neko2.webm")

if [[ "$clip1_frames" -le 0 || "$clip2_frames" -le 0 ]]; then
  printf 'ffmpeg did not generate both videos\n' >&2
  exit 1
fi

cat >"$processed_dir/manifest.conf" <<EOF
version=2
asset_format=video_side_by_side
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

printf 'Generated side-by-side videos: neko1=%s frames, neko2=%s frames.\n' "$clip1_frames" "$clip2_frames"
du -sh "$processed_dir"
