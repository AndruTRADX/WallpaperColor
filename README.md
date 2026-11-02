# Wallpaper Color - KDE Plasma

Paints your PC's RGB components with the color of the wallpaper on screen (including the current
slide of a KDE Plasma slideshow), through OpenLinkHub's built-in OpenRGB SDK server
(stock package; turn on "OpenRGB Integration" per device in its WebUI).

- `wallpapercolord`: a minimal background service (no terminal dependencies)
- `wallpapercolor`: a terminal UI (FTXUI) to configure and watch it, plus commands

Written in C++23. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) explains how it works and
where everything is.

## Install

1. In OpenLinkHub, set `"enableOpenRGBTargetServer": true` in `/var/lib/openlinkhub/config.json`
   (restart it), then turn on **OpenRGB Integration** for each device in the WebUI.
2. `sudo pacman -S --needed cmake ninja stb tomlplusplus nlohmann-json cli11 sdbus-cpp doctest libwebp`
   (FTXUI is downloaded by CMake on the first build).
3. `./install.sh`: builds, runs the tests, installs `~/.local/bin/wallpapercolor{,d}` and starts the
   `wallpapercolord` user service. Logs: `journalctl --user -u wallpapercolord -f`.

## Use

```sh
wallpapercolor                               # terminal UI: status, device preview, settings
wallpapercolor status                        # what the service is doing (also pause/resume/apply)
wallpapercolor extract -p picture.png        # colours of a picture (main + arc)
wallpapercolor devices                       # RGB devices the server exposes
wallpapercolor set --image picture.png       # paint them once (--arc / --single / --split)
wallpapercolor config                        # effective config
```

Config: `~/.config/wallpapercolor/config.toml` (every key optional, the service reloads it):

```toml
[led]
mode = "arc"        # "arc": three colours as a gradient; "single": one colour;
                    # "split": one colour per kind of zone (RAM, pump, fans)
fade = 0.6          # seconds a colour change takes (0 = at once, at most 1.5)

[openrgb]
address = "127.0.0.1:6743"
```
