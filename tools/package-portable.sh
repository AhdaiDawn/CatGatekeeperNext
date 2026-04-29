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
screen_index=0
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
command_name="cat-gatekeeper"
command_marker="# cat-gatekeeper portable command shim"

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

After start, use:
  cat-gatekeeper {status|trigger|dismiss|quit}
EOF_USAGE
}

resolve_command_dir() {
    if [ -n "${CAT_GATEKEEPER_COMMAND_DIR:-}" ]; then
        printf '%s\n' "$CAT_GATEKEEPER_COMMAND_DIR"
        return
    fi
    [ -n "${HOME:-}" ] || die "HOME is required to register the control command"
    printf '%s/.local/bin\n' "$HOME"
}

registered_command_path() {
    printf '%s/%s\n' "$(resolve_command_dir)" "$command_name"
}

is_managed_command() {
    local path="$1"
    [ -f "$path" ] || return 1
    grep -Fqx "$command_marker" "$path"
}

registered_command_targets_this_package() {
    local path="$1"
    [ -f "$path" ] || return 1
    grep -Fqx "# target: $app_dir/start.sh" "$path"
}

path_contains_command_dir() {
    local command_dir="$1"
    case ":${PATH:-}:" in
        *":$command_dir:"*) return 0 ;;
        *) return 1 ;;
    esac
}

register_command() {
    local command_dir command_path
    command_dir="$(resolve_command_dir)"
    command_path="$(registered_command_path)"

    mkdir -p "$command_dir"
    if [ -e "$command_path" ] && ! is_managed_command "$command_path"; then
        die "$command_path already exists and is not managed by this package"
    fi

    {
        printf '#!/usr/bin/env bash\n'
        printf '%s\n' "$command_marker"
        printf '# target: %s\n' "$app_dir/start.sh"
        printf 'export CAT_GATEKEEPER_COMMAND_DIR=%q\n' "$command_dir"
        printf 'exec %q "$@"\n' "$app_dir/start.sh"
    } > "$command_path"
    chmod 0755 "$command_path"

    printf 'Control command registered: %s\n' "$command_path"
    if ! path_contains_command_dir "$command_dir"; then
        printf 'Add this to your shell PATH to use "%s" directly:\n' "$command_name"
        printf '  export PATH="%s:$PATH"\n' "$command_dir"
    fi
}

unregister_command() {
    local command_path
    command_path="$(registered_command_path)"

    if [ ! -e "$command_path" ]; then
        return 0
    fi
    if is_managed_command "$command_path" && registered_command_targets_this_package "$command_path"; then
        rm -f "$command_path"
        printf 'Control command unregistered: %s\n' "$command_path"
    else
        printf 'Control command left in place: %s\n' "$command_path" >&2
    fi
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
        register_command
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
        register_command
        trap unregister_command EXIT
        "$daemon"
        exit "$?"
        ;;
    status|trigger|dismiss)
        [ "$#" -eq 1 ] || die "$command does not accept extra arguments"
        prepare
        exec "$ctl" "$command"
        ;;
    quit)
        [ "$#" -eq 1 ] || die "quit does not accept extra arguments"
        prepare
        set +e
        "$ctl" quit
        result="$?"
        set -e
        unregister_command
        exit "$result"
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
- ffmpeg libraries with the libvpx-vp9 decoder

Start:
  ./start.sh

After start:
  cat-gatekeeper status
  cat-gatekeeper trigger
  cat-gatekeeper dismiss
  cat-gatekeeper quit

Command registration:
  ./start.sh writes cat-gatekeeper to \$HOME/.local/bin by default.
  Set CAT_GATEKEEPER_COMMAND_DIR to choose another directory.
  If the command directory is not in PATH, start.sh prints the export line to add.
  cat-gatekeeper quit stops the daemon and removes the registered command.

Foreground:
  ./start.sh --foreground

Direct control:
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
