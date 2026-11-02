# WallpaperColor

Paints your PC's RGB components with the colour of the wallpaper on screen (including the current
slide of a KDE Plasma slideshow), through OpenLinkHub's built-in OpenRGB SDK server
(stock package; turn on "OpenRGB Integration" per device in its WebUI).

- `wallpapercolord`: a minimal background service (no terminal dependencies)
- `wallpapercolor`: a terminal UI (ratatui) to configure and watch it

Status: phases 0–2 of [docs/PLAN.md](docs/PLAN.md) done (colour extraction, OpenRGB client).

```sh
cargo build --release
./target/release/wallpapercolor extract -p ~/Pictures/wallpapers/*.png   # colour of a picture
./target/release/wallpapercolor devices                                  # exposed RGB devices
./target/release/wallpapercolor set --image picture.png                  # paint them
```
