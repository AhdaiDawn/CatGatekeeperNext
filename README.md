# CatGatekeeperNext

KDE Plasma Wayland reminder daemon with a transparent cat overlay.

The daemon counts unlocked session time. When the interval is reached, it starts `cat-gatekeeper-overlay`; after the overlay exits, the timer restarts. The overlay assets are embedded in the overlay executable.

## Requirements

Runtime:

- `systemd-libs`
- `qt6-base`
- `layer-shell-qt`
- FFmpeg libraries

Build:

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

## Build

```sh
cd CatGatekeeperNext
./tools/preprocess-assets.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Outputs:

```text
build/cat-gatekeeperd
build/cat-gatekeeperctl
build/cat-gatekeeper-overlay
```

Common commands:

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

Example:

```conf
interval_minutes=30
sleep_seconds=300
idle_reset_seconds=0
```

Fields:

- `interval_minutes`: `1..1440`, default `30`.
- `sleep_seconds`: `1..3600`, default `300`.
- `idle_reset_seconds`: parsed but not implemented, default `0`.

Unknown keys are ignored with a warning. Duplicate keys, empty values, and invalid ranges are errors.

## Run

The daemon must run inside a KDE Wayland user session with `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, `XDG_CURRENT_DESKTOP`, and `XDG_SESSION_TYPE`.

```sh
build/cat-gatekeeperd
```

Control:

```sh
build/cat-gatekeeperctl status
build/cat-gatekeeperctl trigger
build/cat-gatekeeperctl dismiss
build/cat-gatekeeperctl quit
```

Test overlay directly:

```sh
QT_QPA_PLATFORM=wayland build/cat-gatekeeper-overlay --sleep-seconds 10 --screen primary
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
./ctl.sh status
./ctl.sh trigger
./ctl.sh quit
```

The package does not write to `/usr`. Runtime libraries are not bundled.

## systemd User Service

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
- `69`: missing Wayland/logind/KDE session environment.
- `70`: other program error.

`cat-gatekeeper-overlay`:

- `0`: completed.
- `2`: invalid arguments.
- `3`: invalid bundled assets.
- `4`: layer-shell setup failed.

## Limits

- KDE Plasma Wayland only.
- Primary screen only.
- No settings UI.
- No real idle detection yet.

## Acknowledgement

The idea for this project came from https://x.com/konekone2026/status/2048215520965709940. Thanks to @konekone2026.
