#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/package-portable.sh [options]

Build a portable CatGatekeeperNext release directory and tar.gz archive.

Options:
  --build-dir DIR       CMake build directory. Default: build/portable-release
  --dist-dir DIR        Output directory. Default: dist
  --build-type TYPE     CMake build type. Default: Release
  --package-name NAME   Release directory/archive name.
  --skip-build          Use existing binaries from --build-dir.
  --skip-assets         Do not regenerate processed assets.
  --skip-archive        Create the release directory only.
  -h, --help            Show this help text.
EOF
}

log() {
    printf 'package-portable: %s\n' "$*"
}

die() {
    printf 'package-portable: error: %s\n' "$*" >&2
    exit 1
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "$1 is required"
}

make_absolute() {
    realpath -m "$1"
}

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$project_root/build/portable-release"
dist_dir="$project_root/dist"
build_type="Release"
package_name=""
skip_build=0
skip_assets=0
skip_archive=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)
            [ "$#" -ge 2 ] || die "--build-dir needs a value"
            build_dir="$2"
            shift 2
            ;;
        --dist-dir)
            [ "$#" -ge 2 ] || die "--dist-dir needs a value"
            dist_dir="$2"
            shift 2
            ;;
        --build-type)
            [ "$#" -ge 2 ] || die "--build-type needs a value"
            build_type="$2"
            shift 2
            ;;
        --package-name)
            [ "$#" -ge 2 ] || die "--package-name needs a value"
            package_name="$2"
            shift 2
            ;;
        --skip-build)
            skip_build=1
            shift
            ;;
        --skip-assets)
            skip_assets=1
            shift
            ;;
        --skip-archive)
            skip_archive=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

require_cmd realpath
if [ "$skip_build" -eq 0 ]; then
    require_cmd cmake
fi
if [ "$skip_build" -eq 0 ] && [ "$skip_assets" -eq 0 ]; then
    require_cmd find
fi
if [ "$skip_archive" -eq 0 ]; then
    require_cmd tar
fi

build_dir="$(make_absolute "$build_dir")"
dist_dir="$(make_absolute "$dist_dir")"

version="$(sed -n 's/^project(CatGatekeeperNext VERSION \([^ ]*\).*/\1/p' "$project_root/CMakeLists.txt" | head -n 1)"
if [ -z "$version" ]; then
    version="0.0.0"
fi

arch="$(uname -m)"
if [ -z "$package_name" ]; then
    package_name="cat-gatekeeper-portable-$version-$arch"
fi

case "$package_name" in
    ""|*/*|*..*)
        die "package name must be a plain directory name"
        ;;
esac

package_dir="$dist_dir/$package_name"
archive_path="$dist_dir/$package_name.tar.gz"
assets_manifest="$project_root/assets/processed/manifest.conf"

if [ "$skip_build" -eq 0 ] && [ "$skip_assets" -eq 0 ]; then
    need_assets=0
    if [ ! -f "$assets_manifest" ]; then
        need_assets=1
    else
        while IFS= read -r -d '' source_file; do
            if [ "$source_file" -nt "$assets_manifest" ]; then
                need_assets=1
                break
            fi
        done < <(find "$project_root/assets/source" -type f -print0)
    fi

    if [ "$need_assets" -eq 1 ]; then
        log "generating processed assets"
        "$project_root/tools/preprocess-assets.sh"
    fi
fi

if [ "$skip_build" -eq 0 ]; then
    [ -f "$assets_manifest" ] || die "missing assets/processed/manifest.conf; run tools/preprocess-assets.sh"
fi

if [ "$skip_build" -eq 0 ]; then
    log "configuring $build_type build in $build_dir"
    cmake -S "$project_root" -B "$build_dir" -DCMAKE_BUILD_TYPE="$build_type"
    log "building release binaries"
    cmake --build "$build_dir"
fi

for binary in cat-gatekeeperd cat-gatekeeperctl cat-gatekeeper-overlay; do
    [ -x "$build_dir/$binary" ] || die "missing executable: $build_dir/$binary"
done

log "creating portable directory $package_dir"
rm -rf "$package_dir"
mkdir -p \
    "$package_dir/bin" \
    "$package_dir/logs"

install -m 0755 \
    "$build_dir/cat-gatekeeperd" \
    "$build_dir/cat-gatekeeperctl" \
    "$build_dir/cat-gatekeeper-overlay" \
    "$package_dir/bin/"

cat > "$package_dir/settings.conf" <<'EOF'
interval_minutes=30
sleep_seconds=300
idle_reset_seconds=0
EOF

cat > "$package_dir/start.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
daemon="$app_dir/bin/cat-gatekeeperd"
ctl="$app_dir/bin/cat-gatekeeperctl"
overlay="$app_dir/bin/cat-gatekeeper-overlay"
settings="$app_dir/settings.conf"
config_home="$app_dir/config-home"
config_file="$config_home/cat-gatekeeper/config.conf"
log_file="$app_dir/logs/cat-gatekeeperd.log"

die() {
    printf 'cat-gatekeeper: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<EOF_USAGE
Usage:
  $0 [start]
  $0 --foreground
  $0 {status|trigger|dismiss|quit}
EOF_USAGE
}

prepare() {
    [ -x "$daemon" ] || die "missing bin/cat-gatekeeperd"
    [ -x "$ctl" ] || die "missing bin/cat-gatekeeperctl"
    [ -x "$overlay" ] || die "missing bin/cat-gatekeeper-overlay"
    [ -r "$settings" ] || die "missing settings.conf"

    mkdir -p "$app_dir/logs" "$config_home/cat-gatekeeper"
    cp "$settings" "$config_file"
    export XDG_CONFIG_HOME="$config_home"
    export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"
}

command="${1:-start}"
case "$command" in
    start)
        [ "$#" -le 1 ] || die "start does not accept extra arguments"
        prepare
        if "$ctl" status >/dev/null 2>&1; then
            printf 'cat-gatekeeperd is already running.\n'
            exit 0
        fi
        nohup "$daemon" >> "$log_file" 2>&1 &
        printf 'cat-gatekeeperd started with pid %s\n' "$!"
        printf 'Log: %s\n' "$log_file"
        ;;
    --foreground)
        [ "$#" -eq 1 ] || die "--foreground does not accept extra arguments"
        prepare
        exec "$daemon"
        ;;
    status|trigger|dismiss|quit)
        [ "$#" -eq 1 ] || die "$command does not accept extra arguments"
        prepare
        exec "$ctl" "$command"
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        die "unknown command: $command"
        ;;
esac
EOF

cat > "$package_dir/README-portable.txt" <<EOF
CatGatekeeperNext portable package
Version: $version

Target: KDE Plasma Wayland.
No files are installed under /usr. Assets are built into bin/cat-gatekeeper-overlay.

Required runtime libraries:
- systemd-libs
- qt6-base
- layer-shell-qt
- ffmpeg libraries

Start:
  ./start.sh

Foreground:
  ./start.sh --foreground

Control:
  ./start.sh status
  ./start.sh trigger
  ./start.sh dismiss
  ./start.sh quit

Settings: settings.conf

Idea: https://x.com/konekone2026/status/2048215520965709940
Thanks to @konekone2026.
EOF

chmod 0755 \
    "$package_dir/start.sh"

if [ "$skip_archive" -eq 0 ]; then
    log "creating archive $archive_path"
    tar -C "$dist_dir" -czf "$archive_path" "$package_name"
fi

log "portable package ready"
printf '%s\n' "$package_dir"
if [ "$skip_archive" -eq 0 ]; then
    printf '%s\n' "$archive_path"
fi
