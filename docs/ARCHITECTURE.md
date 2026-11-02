# How WallpaperColor works

## The two programs

- **`wallpapercolord`** (the service): runs in the background as a systemd user unit. It finds the
  picture on screen, takes its colours and sends them to the RGB devices. It has no terminal code.
- **`wallpapercolor`** (the client): the terminal UI and the commands (`status`, `set`, `extract`…).
  The service works without it.

They talk over a Unix socket (`$XDG_RUNTIME_DIR/wallpapercolor.sock`): one JSON line in, one
JSON line out. Settings never go through the socket. The client edits the config file and the
service notices and reloads it.

## From wallpaper to LEDs

```text
 slideshow folder ──inotify "file opened"──┐
 ~/.config (settings, kdeglobals) ─inotify─┤
 Plasma D-Bus signals ─────────────────────┼─▶ event queue ─▶ main loop
 client requests (socket) ─────────────────┘                     │
                                                                  ▼
   1. ask Plasma over D-Bus which picture is on screen   (kde/plasma.cpp)
   2. same picture and settings as last time? stop
   3. decode it, shrink it to 96×96, pick colours        (core/image.cpp, core/extract.cpp)
   4. decide every LED's colour (arc / single / split)   (core/paint.cpp, core/arc.cpp)
   5. fade to them on OpenLinkHub's OpenRGB SDK server   (daemon/main.cpp, openrgb/client.cpp)
      and on the motherboard's RGB controller            (fusion2/board.cpp)
```

Every event is only a **trigger**. The answer always comes from Plasma (step 1), so a false
alarm costs one D-Bus call and changes nothing. A burst of events (Plasma rescanning a folder
opens every file in it) is merged by waiting until 0.8 s pass with no new event.

A fade sends 25 frames a second, blending each LED from what the service sent last to the new
colour, for `[led] fade` seconds (at most 1.5, so a request from the terminal UI never waits
long). Then the service sleeps again.

While nothing happens, every thread is asleep in the kernel: 0 % CPU, about 7.5 MB of RAM. If
OpenLinkHub or Plasma don't answer, the service retries after 2 s, 4 s, 8 s … up to 60 s.

## Why it is built this way

| Choice | Reason |
| --- | --- |
| OpenRGB **SDK server** of OpenLinkHub (port 6743), not its REST API | REST `/api/color/all` answered "success" without changing the LEDs. The SDK server works. It only shows devices whose **"OpenRGB Integration"** switch is on in the OpenLinkHub WebUI. |
| D-Bus `org.kde.PlasmaShell.wallpaper(0)` | Its `Image` field is the picture on screen right now, including the current slide of a slideshow. |
| inotify on the slideshow folders | Plasma sends no D-Bus signal when the slide changes, but plasmashell opens the next picture, and inotify reports that. |
| The service ignores opens of the picture already on screen | It opens that picture itself to read it. Otherwise each retry would trigger the next one immediately. |
| Watching `plasma-org.kde.plasma.desktop-appletsrc` and `kdeglobals` | Changing the wallpaper in the settings doesn't always send a D-Bus signal. A light/dark switch can pick another picture of a wallpaper package. |
| `malloc_trim(0)` after each picture | It gives the decoded picture's memory (tens of MB for 4K) back to the system. |
| The format is read from the file's content | Many wallpapers are PNGs named `.jpg`. |
| The motherboard is driven directly (`/dev/hidraw`), not through OpenRGB | OpenRGB would have to run all the time as a second background process. The protocol (from OpenRGB's source) is short for single-colour zones. `udev/70-wallpapercolor.rules` gives the logged-in user access. Nothing is saved to the board's flash. |

How the colours are picked (palette, ignoring grey, specks and skin, pastels, the three
"arc" colours) is explained step by step at the top of `src/core/extract.hpp`.

## Folders

```text
src/
  core/     colours, gradients, painting, image decoding, colour extraction, config file, socket protocol
  openrgb/  OpenRGB SDK client (TCP, protocol version 4)
  fusion2/  Gigabyte RGB Fusion 2 motherboard RGB (USB HID)
  kde/      Plasma over D-Bus, wallpaper packages, light/dark scheme
  util/     sockets, inotify, the thread-safe queue, UTF-8, errors, log
  daemon/   wallpapercolord: the main loop
  client/   wallpapercolor: commands (main.cpp), terminal UI (tui.cpp), config editor (settings.cpp)
tests/      one test file per module (doctest)
systemd/    the user unit installed by install.sh
udev/       the rule that lets your user use the motherboard's RGB controller
```

Every library is a CMake target (`src/CMakeLists.txt`): `wpc_core` doesn't know about OpenRGB
or D-Bus, and the service never links the terminal UI.

Conventions: functions that can fail throw `wpc::Error` with a message ready for the user;
values that may be missing are `std::optional`. Colour maths uses `float`.

## Adding things

- **Another desktop** (GNOME, Hyprland…): a new folder next to `kde/` with the same job:
  "which picture is on screen" and "tell me when it may have changed". Then use it in
  `daemon/main.cpp`.
- **Other hardware**: a folder like `fusion2/` with a class that can show colours, added as
  one more `Output` in `Daemon::send` (`daemon/main.cpp`). The motherboard's ARGB headers
  (addressable strips) would need per-LED packets in `fusion2/board.cpp`.
- **Another way to spread colours over the LEDs**: a new `Mode` and its case in
  `core/paint.cpp`. The service, `set` and the terminal UI's preview all paint through it.
- **Another way to pick colours**: `core/extract.cpp`. `wallpapercolor extract -p picture` shows
  the result and the palette it came from.

## Build and test

```sh
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure
build/src/wallpapercolor extract -p ~/Pictures/wallpapers/some.jpg
```

`./install.sh` does the same, then installs both programs and restarts the service.
