# WallpaperColor

Paints your PC's RGB components with the colour of the wallpaper on screen (including the current
slide of a KDE Plasma slideshow), through OpenLinkHub's built-in OpenRGB SDK server
(stock package; turn on "OpenRGB Integration" per device in its WebUI).

- `wallpapercolord`: a minimal background service (no terminal dependencies)
- `wallpapercolor`: a terminal UI (ratatui) to configure and watch it

Status: phases 0–3 of [docs/PLAN.md](docs/PLAN.md) done: the service works; the terminal UI
(phase 4) is next.

## Install

1. In OpenLinkHub, set `"enableOpenRGBTargetServer": true` in `/var/lib/openlinkhub/config.json`
   (restart it), then turn on **OpenRGB Integration** for each device in the WebUI.
2. `./install.sh`: builds, installs `~/.local/bin/wallpapercolor{,d}` and starts the
   `wallpapercolord` user service. Logs: `journalctl --user -u wallpapercolord -f`.

## Use

```sh
wallpapercolor extract -p picture.png        # colours of a picture (main + arc)
wallpapercolor devices                       # RGB devices the server exposes
wallpapercolor set --image picture.png       # paint them once (--arc / --single)
wallpapercolor config                        # effective config
```

Config: `~/.config/wallpapercolor/config.toml` (every key optional, the service reloads it):

```toml
[led]
mode = "arc"        # or "single"

[openrgb]
address = "127.0.0.1:6743"
```
