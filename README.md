# CatGatekeeperNext

A tiny screen guardian for KDE Plasma Wayland, GNOME Wayland, and Windows. It quietly watches your unlocked time, then sends a transparent cat onto your desktop when it is time to step away for a bit.

[![CatGatekeeperNext demo](assets/demo.gif)](assets/demo.webm)

The cat appears only for the configured reminder window and disappears on its own when the break timer is done. On KDE the layer-shell backend passes clicks through to apps underneath; on GNOME the frameless window fallback requests the same Qt input transparency, but the compositor may handle it differently.

## Acknowledgement

The idea for this project came from https://x.com/konekone2026/status/2048215520965709940. Thanks to @konekone2026.

## Requirements

Linux runtime:

- `systemd-libs`
- `qt6-base`
- `layer-shell-qt`
- FFmpeg libraries with the `libvpx-vp9` decoder

KDE Plasma uses the layer-shell overlay backend. GNOME Wayland uses a frameless Qt window fallback because GNOME Shell does not expose layer-shell to normal clients.

Linux build:

- `cmake`
- `gcc`
- `g++`
- `extra-cmake-modules`
- `pkgconf`
- `ffmpeg`

Arch Linux:

```sh
sudo pacman -S cmake gcc extra-cmake-modules pkgconf systemd-libs qt6-base layer-shell-qt ffmpeg
```

Windows runtime:

- Windows 10 or newer desktop session
- Qt 6 Widgets runtime libraries
- FFmpeg libraries with the `libvpx-vp9` decoder

Windows build is currently aimed at MSYS2 MinGW. From a UCRT64 shell:

```sh
pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-ffmpeg
```

## Build

```sh
cd CatGatekeeperNext
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The overlay assets are tracked under `assets/` and embedded into `cat-gatekeeper-overlay`.


Outputs:

```text
build/cat-gatekeeperd
build/cat-gatekeeperctl
build/cat-gatekeeper-overlay
```

On Windows, the output files use the `.exe` suffix.

Common Linux commands:

```sh
just build
just check
just overlay 10
just status
```

## Configuration

Path:

```text
$XDG_CONFIG_HOME/cat-gatekeeper/config.conf
```

Fallback:

```text
~/.config/cat-gatekeeper/config.conf
```

Windows path:

```text
%APPDATA%\CatGatekeeper\config.conf
```

Windows fallback:

```text
%USERPROFILE%\.config\cat-gatekeeper\config.conf
```

Example:

```conf
interval_minutes=30
sleep_seconds=300
screen_index=0
idle_reset_seconds=0
```

Fields:

- `interval_minutes`: `1..1440`, default `30`.
- `sleep_seconds`: `1..3600`, default `300`.
- `screen_index`: non-negative integer, default `0`; selects the Qt screen index for the overlay and falls back to `0` when that screen is unavailable.
- `idle_reset_seconds`: parsed but not implemented, default `0`.

Unknown keys are ignored with a warning. Duplicate keys, empty values, and invalid ranges are errors.

## Run

On Linux, the daemon must run inside a KDE Plasma or GNOME Wayland user session with `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, `XDG_CURRENT_DESKTOP`, and `XDG_SESSION_TYPE`.

```sh
build/cat-gatekeeperd
```

On Windows, run from a normal unlocked desktop session:

```bat
build\cat-gatekeeperd.exe
```

The Windows daemon is built as a background GUI-subsystem executable, so launching it directly does not keep a command-line window open.

Control:

```sh
build/cat-gatekeeperctl status
build/cat-gatekeeperctl trigger
build/cat-gatekeeperctl dismiss
build/cat-gatekeeperctl quit
```

Windows:

```bat
build\cat-gatekeeperctl.exe status
build\cat-gatekeeperctl.exe trigger
build\cat-gatekeeperctl.exe dismiss
build\cat-gatekeeperctl.exe quit
```

Test overlay directly:

```sh
QT_QPA_PLATFORM=wayland build/cat-gatekeeper-overlay --sleep-seconds 10 --screen 0
QT_QPA_PLATFORM=wayland build/cat-gatekeeper-overlay --sleep-seconds 10 --screen 0 --backend window
```

Windows:

```bat
build\cat-gatekeeper-overlay.exe --sleep-seconds 10 --screen 0
```

## Install

```sh
cmake --install build
```

Install under `~/.local`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
```

`cat-gatekeeperd` finds `cat-gatekeeper-overlay` in the same directory as itself.

## Portable Package

The portable packaging script is Linux-only for now. GitHub Actions builds the Windows package directly in CI.

```sh
./tools/package-portable.sh
```

Or:

```sh
just package
```

Output:

```text
dist/cat-gatekeeper-portable-<version>-<arch>/
dist/cat-gatekeeper-portable-<version>-<arch>.tar.gz
```

Run from the unpacked directory:

```sh
./start.sh
cat-gatekeeper status
cat-gatekeeper trigger
cat-gatekeeper quit
```

`./start.sh` registers a user command at `$HOME/.local/bin/cat-gatekeeper`, or under `CAT_GATEKEEPER_COMMAND_DIR` when that environment variable is set. If that directory is not in `PATH`, the script prints the exact `export PATH=...` line to add. `cat-gatekeeper quit` stops the daemon and removes the registered command.

The package does not write to `/usr`. Runtime libraries are not bundled.

Windows zip packages include convenience scripts in the package root:

```bat
cat-gatekeeper.bat
start.bat
status.bat
trigger.bat
dismiss.bat
quit.bat
```

`start.bat` launches `bin\cat-gatekeeperd.exe` in the background. `cat-gatekeeper.bat` also accepts `start`, `status`, `trigger`, `dismiss`, and `quit`; the other scripts are shortcuts for those commands.

## GitHub Release

GitHub Actions builds Linux and Windows packages on pushes and pull requests. Pushing a `v*` tag creates a GitHub Release with:

- `cat-gatekeeper-linux-x86_64-portable.tar.gz`
- `cat-gatekeeper-linux-x86_64-portable.tar.gz.sha256`
- `cat-gatekeeper-windows-ucrt64-portable.zip`
- `cat-gatekeeper-windows-ucrt64-portable.zip.sha256`

## systemd User Service

The systemd service is Linux-only.

For installed builds, copy or install the generated service file to `~/.config/systemd/user/`, then run:

```sh
systemctl --user import-environment WAYLAND_DISPLAY XDG_RUNTIME_DIR XDG_CURRENT_DESKTOP XDG_SESSION_TYPE XDG_SESSION_ID
systemctl --user daemon-reload
systemctl --user enable --now cat-gatekeeper.service
```

Logs:

```sh
journalctl --user -u cat-gatekeeper.service -f
```

## Exit Codes

`cat-gatekeeperd`:

- `0`: normal exit.
- `64`: invalid config.
- `66`: missing or non-executable sibling overlay.
- `69`: missing platform session environment.
- `70`: other program error.

`cat-gatekeeper-overlay`:

- `0`: completed.
- `2`: invalid arguments.
- `3`: invalid bundled assets.
- `4`: overlay setup failed.

## Limits

- KDE Plasma Wayland and GNOME Wayland are supported.
- GNOME uses a frameless window fallback, so compositor-level stacking and input pass-through behavior can differ from KDE's layer-shell backend.
- Windows support targets normal desktop sessions and does not display over the secure lock screen.
- Screen selection uses the configured `screen_index`; unavailable indexes fall back to `0`.
- No settings UI.
- No real idle detection yet.
