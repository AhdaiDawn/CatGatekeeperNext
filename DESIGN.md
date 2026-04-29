# Design

CatGatekeeperNext is a small KDE Plasma and GNOME Wayland reminder. It has two runtime processes:

- `cat-gatekeeperd`: C daemon, session timer, logind lock-state handling, local control socket.
- `cat-gatekeeper-overlay`: Qt overlay, optional LayerShellQt integration, FFmpeg video decoding, transparent input-pass-through window.

The daemon starts the overlay only while a reminder is visible. The overlay exits on its own after the configured duration.

## Platform

Target:

- Linux
- KDE Plasma or GNOME
- Wayland
- systemd user session

Required runtime libraries:

- `libsystemd`
- Qt 6 Widgets
- LayerShellQt
- FFmpeg/libav with `libvpx-vp9`

## Build

CMake builds:

- `cat-gatekeeperd`
- `cat-gatekeeperctl`
- `cat-gatekeeper-overlay`

Runtime assets are tracked under `assets/`:

```text
assets/manifest.conf
assets/neko1.webm
assets/neko2.webm
```

Those files are embedded into `cat-gatekeeper-overlay` with Qt resource files. They are not installed or packaged as external runtime files.

## Timer

The daemon counts monotonic time only while the session is unlocked.

Rules:

- On startup, if the session is unlocked, counting begins immediately.
- On lock, counting resets and pauses.
- On unlock, counting restarts from zero.
- When `interval_minutes` is reached, the daemon starts the overlay.
- After the overlay exits, counting restarts from zero.
- Manual `trigger` starts one reminder only when counting.

## Configuration

Config path:

```text
$XDG_CONFIG_HOME/cat-gatekeeper/config.conf
```

Fallback:

```text
~/.config/cat-gatekeeper/config.conf
```

Supported keys:

- `interval_minutes`: `1..1440`, default `30`.
- `sleep_seconds`: `1..3600`, default `300`.
- `screen_index`: non-negative integer, default `0`; unavailable indexes fall back to `0`.
- `idle_reset_seconds`: `0..86400`, parsed but not implemented.

Parsing rules:

- Format is `key=value`.
- Empty lines and `#` lines are ignored.
- Duplicate known keys are errors.
- Empty values are errors.
- Unknown keys are warnings and ignored.

## Overlay Resolution

`cat-gatekeeperd` resolves its own path with `/proc/self/exe`, then starts:

```text
<daemon-directory>/cat-gatekeeper-overlay
```

This keeps installed builds and portable builds relocatable as long as both executables stay in the same directory.

## Control

The daemon listens on:

```text
$XDG_RUNTIME_DIR/cat-gatekeeper/control.sock
```

Supported commands:

- `status`
- `trigger`
- `dismiss`
- `quit`

`cat-gatekeeperctl` is the only control client.

## Overlay

The overlay command line is:

```sh
cat-gatekeeper-overlay --sleep-seconds <seconds> --screen <screen_index>
```

The overlay:

- loads embedded alpha WebM assets;
- decodes VP9 alpha frames with FFmpeg/libvpx;
- draws a full-screen overlay;
- uses layer-shell on KDE Plasma;
- uses a frameless Qt window fallback on GNOME;
- requests mouse input pass-through to windows below it;
- exits after the intro plus `sleep_seconds`.

## Packaging

`tools/package-portable.sh` creates:

```text
dist/cat-gatekeeper-portable-<version>-<arch>/
dist/cat-gatekeeper-portable-<version>-<arch>.tar.gz
```

The portable package contains:

- `bin/`
- `settings.conf`
- `start.sh`
- `README-portable.txt`

On `start`, `start.sh` registers a user command named `cat-gatekeeper` under `$HOME/.local/bin` by default, or `CAT_GATEKEEPER_COMMAND_DIR` when set. The command delegates back to the package's `start.sh`, so users can run `cat-gatekeeper status`, `cat-gatekeeper trigger`, `cat-gatekeeper dismiss`, and `cat-gatekeeper quit` from any shell where that directory is in `PATH`. `quit` removes the registered command after sending the daemon quit request.

It does not bundle system libraries.

## Exit Codes

`cat-gatekeeperd`:

- `0`: normal exit.
- `64`: invalid config.
- `66`: sibling overlay missing or not executable.
- `69`: required Wayland/logind/desktop session unavailable.
- `70`: other program error.

`cat-gatekeeper-overlay`:

- `0`: completed.
- `2`: invalid arguments.
- `3`: invalid bundled assets.
- `4`: overlay window setup failed.

## Limits

- KDE Plasma Wayland and GNOME Wayland are supported.
- GNOME uses the frameless window fallback because stock GNOME Shell does not expose layer-shell to normal clients.
- Screen selection by numeric Qt screen index.
- No settings UI.
- No browser tracking.
- No input blocking.
- No real idle detection yet.

## Acknowledgement

The idea came from https://x.com/konekone2026/status/2048215520965709940. Thanks to @konekone2026.
