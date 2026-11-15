# How WallpaperColor is built

Read **The architecture** and **The modules** before writing anything: `ctest` checks the
dependency rules on every build.

## The parts

- **`wallpapercolord`**, the service (`src/daemon/`): runs in the background as a systemd user
  unit. It finds the picture on screen, takes its colours and paints the RGB hardware with them.
- **`wallpapercolor`**, the terminal (`src/terminal/`): the terminal UI and the commands
  (`status`, `set`, `devices`, `drivers`, `extract`, `config`, `pause`, `resume`, `apply`).
  The service works without it.
- **The driver modules** (`src/drivers/<name>/`): one file (`wpc-driver-<name>.so`) per kind
  of hardware, loaded by a program only while it needs it.
- **The picture decoders** (`src/pictures/decoders.cpp`): their own module, `wpc-pictures.so`,
  with the image libraries, loaded only while a picture is read.

The terminal and the service talk over a Unix socket (`$XDG_RUNTIME_DIR/wallpapercolor.sock`):
one JSON line in, one JSON line out. Settings never go through the socket: the terminal edits
the config file, and the service notices and reloads it.

## The architecture

A small system program has different needs from a web API: one process that sleeps until the
kernel wakes it, talks to hardware, desktop and files, and must never crash or go deaf. What
fits it is a **modular monolith** (one program, many small libraries) arranged as a
**functional core with an imperative shell**, with **ports and adapters only where the world
outside really varies**, **plugins behind a narrow ABI**, and **one event loop** (the reactor
pattern, like systemd's sd-event or PipeWire's loop).

### 1. Modules by what they are about, in one direction

Each folder of `src/` is a module: one CMake library, one subject. A change to "how colours are
picked" is in `colours/`, to "how Plasma is asked" in `daemon/kde/`, to "how the Razer keyboard
is driven" in `drivers/razer/`. Folders say what the code is *about*, not what *kind* of code
it is: there is no `contracts/`, `models/` or `features/` folder spread over every part.

The modules depend on each other in one direction only (no cycles); the lower ones know
nothing of the higher ones:

```text
                 utils/                 Linux and C++ building blocks
                   ▲
              drivers/sdk/              plain types every module shares (Rgb, Zone, Device, Driver)
          ▲        ▲          ▲
   drivers/<x>/  colours/   drivers/host/
                 ▲      ▲         ▲
          settings/  protocol/  pictures/
                 ▲      ▲         ▲
        ┌────────┴──────┴─────────┴────────┐
     daemon/                           terminal/
     service/ ◀── kde/, system/        app/ ◀── system/, cli/, tui/
```

A driver module sees only `drivers/sdk/` and `utils/`: never a program, never another driver.
The two programs never include each other.

### 2. A functional core: decisions are pure functions

Everything that decides (which colours a picture gives, which LED gets which colour, how a
fade goes, how long to wait before a retry, what a TOML text or a JSON line means) is a pure
function: data in, data out, no files, no clock, no sockets. It is called directly, with no
interface in front of it, and tested with plain values:

| Pure code | Where |
| --- | --- |
| colour maths, resizing, picking colours, painting zones | `colours/` |
| the settings and their rules, TOML text ↔ settings | `settings/` (`read_settings()` is the one function that opens a file) |
| the messages and their JSON | `protocol/json.cpp` |
| the service's rules: outputs, fades, retries, driver choice | `daemon/service/rules.cpp` |
| each driver's protocol: reports byte by byte, zones, settings | `drivers/<x>/protocol.cpp` |

`ctest` checks that `colours/` and `daemon/service/` include no header that reaches the
operating system.

**Parse, don't validate.** Text from outside becomes typed values once, at the edge, and is
refused there if it is wrong: `parse_settings()` rejects unknown keys, wrong types, NaN and
numbers out of their range (`settings::ranges`), with the line and column. After that, the rest
of the program only ever sees valid settings and never checks them again. The same for a
driver's colour order (`fusion2::colour_positions()`), a server's packets (`openrgb::Reader`),
an inotify buffer, a picture's size.

### 3. An imperative shell: ports where the world varies

The service's logic (`daemon/service/`) and the terminal's (`terminal/app/`) reach the world
only through **ports**: small interfaces, each implemented once for real and once as a fake in
the tests. A port exists only where something outside really changes or must be replaced in a
test:

| Port | What varies | Real one | Fake |
| --- | --- | --- | --- |
| `Desktop` | the desktop environment (Plasma today; GNOME, Hyprland…) | `daemon/kde/plasma` | `FakeDesktop` |
| `FileWatch` | the kernel's file events | `daemon/system/file_watch` (inotify) | `FakeFiles` |
| `Clock` | time (hours of retries in a test run in no time) | `daemon/system/clock` | `FakeClock` |
| `SystemEvents` | sleep and devices plugged in | `daemon/system/logind` | `FakeSystem` |
| `SettingsSource` | the settings file | `daemon/system/settings_source` | `FakeSettings` |
| `RequestServer` | the terminal's socket | `daemon/system/request_server` | (requests sent straight to the loop) |
| `DriverHost` | the hardware, through driver modules | `drivers/host` (`ModuleHost`) | `FakeDrivers` |
| `PictureReader` | picture files, through the decoders' module | `pictures/picture_files` | `FakePictures` |
| `ServiceClient`, `SettingsEditor` | the terminal's socket, the config file | `terminal/system/` | `FakeService`, `TextEditor` |

A port is shaped by what its user needs, not by how the outside works: the service asks the
`Desktop` for the picture on screen (`on_screen()`); that Plasma keeps it in a wallpaper package
with a picture for dark colour schemes is the KDE adapter's business, not the service's.

Inside a driver it is the same: `FusionBoard`, `RazerKeyboard` and `OpenRgbServer` are the
driver's port to its device, so its open / reconnect / close logic is tested with a fake board.

### 4. Plugins behind a narrow, guarded boundary

Driver modules and the picture decoders are shared objects loaded with `dlopen` only while
needed (`utils/shared_library.hpp`), so their code and libraries take no memory the rest of the
time. Their boundary is the SDK (`drivers/sdk/driver.hpp`, `pictures/decoders.hpp`):

- **One exported symbol** (`wpc_driver_info`, `wpc_picture_decoder`), hidden visibility for the
  rest, `-fno-gnu-unique` so `dlclose` really unloads (`cmake/drivers.cmake`).
- **The same build only.** Modules and programs share C++ types, so each module carries the
  build id (version, compiler, the SDK headers' hash) as the first member of what it exports,
  and the programs refuse any other.
- **Plain values cross it.** `Rgb`, `Zone`, `Device` are data with no code to link.
- **No exception outlives its module.** An exception thrown by a module was made by the
  module's code: its vtable and destructor are there. Every call into a module goes through
  `guarded()` (`drivers/host/driver_host.cpp`, and the same in `pictures/picture_files.cpp`),
  which copies it into a `wpc::Error` made by the program while the module is still loaded.
  Without it, a typo in the settings of a driver whose `create()` throws crashed the programs
  (the module was unloaded while the exception travelled).
- **A driver is destroyed before its module is unloaded** (`ModuleHost::Loaded` keeps the
  module first, the driver second: members are destroyed in reverse order).

### 5. One loop, two threads

The service runs on **one event loop** (`utils/event_loop`, epoll) and one more thread:

- **The loop's thread** (`main`) waits in one `epoll_wait` for everything the events hear: the
  inotify fd (`file_watch`), the session bus (`kde/plasma`) and the system bus (`logind`), whose
  messages sd-bus reads and dispatches on this thread. The retry and the 0.3 s of a burst are
  that wait's timeout. Every event's callback and every use case run here, one at a time: the
  service's state needs no lock.
- **The request socket's thread** accepts the terminal's connections and waits for the loop's
  answer. It is a thread of its own so a request still gets "service busy" after 2 s while the
  loop is in a fade or waiting for Plasma; such a request is then given up (`Reply`), so what the
  terminal was told is what happened. It hands the request over through `Notifications`
  (`utils/event_queue`), whose `send()` wakes the loop (an eventfd). The thread is owned: it is
  stopped and joined before anything it uses goes (`RequestReceivedEvent`'s destructor).

Every wait is bounded: Plasma gets 3 s to answer (`PlasmaDesktop::CALL_TIMEOUT`), the OpenRGB
server 2 s to connect and 5 s per read or write, a terminal 2 s to send its request and 2 s to
take the answer, and a burst of notifications is merged for 3 s at most.

### 6. Errors and safety

- Functions that can fail throw `wpc::Error` with a message ready for the user; values that may
  be missing are `std::optional`. Both programs' `main()` also catch any other exception: a bug
  is said, never an abort with no message.
- Every resource has one owner that frees it (RAII): `Fd`, `SharedLibrary`, `std::unique_ptr`,
  the request server's thread. No `new`/`delete`, no raw owning pointer.
- Input from outside is untrusted and bounded: request lines (64 KB), OpenRGB packets (16 MB,
  1024 controllers), config and picture files (regular files only: a FIFO would block for
  ever), picture sizes (decompression bombs are an error, not all the memory), inotify buffers.
- The socket is the user's alone: `0600` in their runtime folder (`/run/user/<uid>`, never a
  name from `$USER`), every client's user id checked. One service per user: a lock next to the
  socket stops a second one before it touches the first one's socket.
- Files are replaced atomically (`utils/files.hpp`): written next to the old one, flushed, then
  renamed; a symlinked config stays a symlink, and keeps its permissions.

### 7. Resources

While nothing happens, the service sleeps in the kernel: 0 % CPU, two threads, about 2.5 MB
of its own memory. What it needs only now and then is loaded only then (the drivers of what is
ticked, the decoders while a picture is read), and `release_free_memory()` (`malloc_trim`)
gives back what a picture or an unloaded driver used. Release builds use `-Os`; the loops over
pixels keep their speed (`-O3` for the decoders, `-O2` for `colours/`).

### SOLID, where it shows

| Principle | In this code |
| --- | --- |
| Single responsibility | One module per subject (a driver's protocol, the KDE desktop, the settings file); one reason for a file to change. |
| Open/closed | New hardware is a new module, new desktop a new `Desktop`, new colour spread a new `Mode`: nothing else changes. |
| Liskov substitution | Every port has a real and a fake implementation that the same use cases run with; the tests prove they are interchangeable. |
| Interface segregation | Ports are small and shaped by their user (`Clock`: three methods; `Driver`: two). Pure code has no interface at all. |
| Dependency inversion | `daemon/service/` and `terminal/app/` depend on their ports; `kde/` and `system/` implement them; `main.cpp` wires them. |

## The modules

```text
src/
  utils/              error, text (cat), log, fd, socket, files (read, atomic write, XDG
                      folders), shared_library (dlopen), event_loop (epoll) + event_queue,
                      channel, inotify, hidraw, memory, utf8, url, toml
  drivers/sdk/        hardware.hpp (Rgb, Component, Shape, Zone, Device), driver_settings.hpp,
                      driver.hpp (ZoneFrame, Driver, DriverInfo, DriverOption)
  colours/            colour (HSV, hex, gradients), picture (resize), extract (palette and the
                      colours of a picture), paint (Painter, calibrations)
  settings/           settings (Config, its parts, ranges), settings_file (TOML ↔ Config, read)
  protocol/           messages, json, socket_path
  pictures/           decoders (the wpc-pictures.so module and its ABI), picture_files
                      (PictureReader, loading the decoders while a picture is read)
  drivers/host/       driver_host (DriverHost, ModuleHost: listing, loading, guarding modules)
  drivers/<name>/     protocol (pure), the device's port and the Driver (driver), the real
                      device (client / board / keyboard), module.cpp
  daemon/
    main.cpp          the composition root
    service/          ports, state, rules (pure), service (the use cases), events (what wakes
                      it), loop (notifications → use cases)
    kde/              plasma (Desktop over D-Bus), wallpaper (packages, colour scheme)
    system/           file_watch, logind, request_server, clock, settings_source
  terminal/
    main.cpp          the command line and the composition root
    app/              ports, settings, hardware, service (the use cases)
    system/           settings_editor (edits config.toml keeping comments), service_client
    cli/              one file per command, and output (printing)
    tui/              app (loop, shared keys), home_page, drivers_page, widgets
```

### Who may include what

`cmake/check_dependencies.py` (in `ctest`) holds this table and fails on any other `#include`:

| Module | May include |
| --- | --- |
| `utils/` | `utils/` |
| `drivers/sdk/` | itself, `utils/error.hpp`, `utils/text.hpp` |
| `colours/` | itself, `drivers/sdk/`, `utils/` (pure: no system headers) |
| `settings/`, `protocol/` | itself, `colours/`, `drivers/sdk/`, `utils/` |
| `pictures/` | itself, `colours/`, `utils/` |
| `drivers/host/` | itself, `drivers/sdk/`, `utils/` |
| `drivers/<name>/` | itself, `drivers/sdk/`, `utils/` |
| `daemon/service/` | itself, `colours/`, `settings/`, `protocol/`, the `PictureReader` and `DriverHost` ports, `drivers/sdk/`, `utils/` (pure of system headers) |
| `daemon/kde/` | itself, `daemon/service/ports.hpp`, `settings/`, `colours/`, `drivers/sdk/`, `utils/` |
| `daemon/system/` | its own header, `daemon/service/ports.hpp`, `settings/`, `colours/`, `drivers/sdk/`, `utils/` |
| `terminal/app/` | itself, `colours/`, `settings/`, `protocol/`, the `PictureReader` and `DriverHost` ports, `drivers/sdk/`, `utils/` |
| `terminal/system/` | its own header, `terminal/app/ports.hpp`, `settings/`, `colours/`, `drivers/sdk/`, `utils/` |
| `terminal/cli/`, `terminal/tui/` | themselves, `terminal/app/`, `colours/`, `settings/`, `protocol/messages.hpp`, `drivers/sdk/` (no `utils/`: showing only) |
| `<program>/main.cpp` | its program and every shared module |

Each module is also its own CMake library (`src/CMakeLists.txt`) linking only what it may use:
the service's logic links no D-Bus, the terminal no socket server or file watcher, the service
no settings editor or terminal UI.

## How to add something

| The code… | is… | goes in |
| --- | --- | --- |
| decides something from data, with no I/O | a pure function | the module of its subject (`colours/`, `settings/`, `rules.cpp`, a driver's `protocol.cpp`) |
| runs a use case: asks ports, applies the rules, records the outcome | a use case | `daemon/service/service.cpp`, `terminal/app/` |
| touches the world (D-Bus, a file, a socket, a device, the clock) | a port's implementation | `daemon/kde/`, `daemon/system/`, `terminal/system/`, behind a port in `ports.hpp` |
| notices that something happened | an event | `daemon/service/events.cpp` |
| shows a result, or turns a command or key into a use case | the terminal's presentation | `terminal/cli/`, `terminal/tui/` |
| is plain code that knows nothing about WallpaperColor | a utility | `utils/` |

Add a port only for a real boundary (I/O you must fake, or a real alternative). A pure function
is called directly.

### Common cases

- **A command of the terminal**: a use case in `terminal/app/`, then `cli/<command>.cpp`
  printing its result, declared in `cli/commands.hpp`, parsed in `terminal/main.cpp`.
- **A request from the terminal to the service**: a `Request` in `protocol/messages.hpp`, its
  name in `protocol/json.cpp`, its case in `run_request()` (`daemon/service/service.cpp`).
- **A setting**: its field in `settings/settings.hpp` (with its range in `settings::ranges`),
  read and written in `settings/settings_file.cpp`; for the terminal UI, a `NumberSetting` in
  `terminal/app/settings.cpp` (a `static_assert` keeps it within the range) and its label in
  `tui/home_page.cpp`.
- **Another desktop** (GNOME, Hyprland…): a `Desktop` implementation next to `daemon/kde/`
  (say `daemon/gnome/`), picked in `daemon/main.cpp`. Nothing else changes.
- **Other hardware**: a new driver module: [ADDING-A-MODULE.md](ADDING-A-MODULE.md).
- **Another way to spread colours over the LEDs**: a `Mode` and its case in
  `colours/paint.cpp`; the service, `set` and the preview all paint through it.
- **Another way to pick colours**: `colours/extract.cpp`; `wallpapercolor extract -p
  <picture>` shows the result and its palette. How the colours are picked (palette, ignoring
  grey, specks and skin, pastels, the three "arc" colours) is explained at the top of
  `colours/extract.hpp`.
- **An event**: a class in `daemon/service/events.hpp` whose `start()` listens through ports and
  calls `notify_` with a notice; the notice in the same header, its case in
  `ServiceLoop::take()`, and its start in `daemon/main.cpp`.

## How it works

### From wallpaper to LEDs

```text
 slideshow folder: a picture opened ──┐
 kdeglobals, Plasma's settings file ──┤  DesktopChangedEvent
 Plasma's D-Bus signals ──────────────┤
 config.toml written ─────────────────┤  SettingsChangedEvent
 waking from sleep, a device added ───┤  HardwareChangedEvent
 a request from the terminal ─────────┤  RequestReceivedEvent
                                      └─▶ notifications ─▶ ServiceLoop (service/loop.cpp)
                                                                   │
   paint_wallpaper (service/service.cpp)                           ▼
   1. what is on screen (the package's picture for the scheme)   Desktop.on_screen
   2. same picture and settings as last time? stop               PictureReader.modified_time
   3. read it small, pick its colours                            PictureReader.read, extract
   4. every LED's colour (arc / single / split)                  rules: plan_outputs, Painter
   5. fade the chosen zones of every loaded driver               DriverHost.show, Clock, rules: fade
   6. give the memory back                                       release_free_memory
```

Every notification is only a **trigger**: the answer always comes from the desktop (step 1),
so a false alarm costs one D-Bus call and changes nothing. A burst (Plasma rescanning a folder
opens every file in it) is merged: the loop waits until 0.3 s pass with no new notification
(3 s at most). The lights start fading about 0.32 s after Plasma opens the next slide.

A fade sends 25 frames a second, blending each LED from what was sent last to its new colour,
for `[led] fade` seconds (at most 1.5); after the first frame, a device whose frame didn't
change is not sent it again. If a driver or Plasma doesn't answer, the service retries after
2 s, 4 s, 8 s … up to 60 s, and paints the other drivers meanwhile.

### Rules that keep the service from going deaf

The service only wakes up for a notification or a due retry. A branch that leaves neither
armed puts it to sleep for good: the lights stay on an old wallpaper until a manual `apply`.
Every change to the loop, the events or the use cases keeps these, and `tests/test_service.cpp`
checks them over time (the `R1`… tests drive `ServiceLoop::step()` with a fake clock):

| Rule | What it means |
| --- | --- |
| R1 | While the service doesn't know what is on screen (Plasma unreachable, no screen), a retry or a signal is armed to wake it. |
| R2 | A due retry is always run and either cleared or moved on; it never stays due (the loop would spin). |
| R3 | The folder watches match the folders on disk: folders created or removed under a slideshow folder too. A tree is watched whole or not at all. |
| R4 | Hardware that comes back (plugged in again, after a suspend) is painted again without waiting for the next wallpaper. Not heard yet: OpenLinkHub restarting (its server sends nothing to a client it no longer has). |
| R5 | What hears events doesn't stop silently: it starts again, or the service says so and stops, and systemd starts it again (`Restart=on-failure`). |

A test marked `doctest::should_fail()` records a known break of a rule; it is unmarked with
the fix.

### Drivers are modules

Every installed module is listed once (loaded briefly to read its `DriverInfo`, then unloaded);
later listings answer from what was read until the file changes. Then:

- the **service** keeps a module loaded only while one of its components (`[components]`,
  ticked on the terminal's Drivers page) is chosen. On every settings reload, `apply_settings`
  loads what is newly needed and unloads the rest; a module whose settings didn't change stays
  loaded, with its connection;
- the **terminal** loads them for `devices`, `set` and the Drivers page, and unloads them at
  once.

Every driver implements `Driver` (`drivers/sdk/driver.hpp`): "which devices and zones do you
have" and "show these colours on these zones". The programs paint zones, never a brand's
devices. Each zone says what it is (`Component`: fans, pump, RAM…) and its shape (ring, line,
spot); the user can correct it with `[[component]]`.

## Why it is built this way

| Choice | Reason |
| --- | --- |
| Modules by subject, one library each, checked by `ctest` | A change has one place; a dependency that isn't allowed doesn't link and fails the check. |
| Pure functions with no interface; ports only at real boundaries | An interface for code with no I/O adds a file, a class and a fake, and tests nothing more than calling it. |
| One `Desktop` port giving the picture on screen | The service doesn't know Plasma's packages or colour schemes; another desktop is one class. |
| Use cases as functions taking only the ports they use | The signature says what a use case touches, and its test fakes only that. |
| Settings checked once, when read (ranges, finite numbers) | A value the service can't use never reaches it: `sample_size = 2000000000` aborted the programs, `fade = nan` was undefined behaviour. |
| Exceptions copied as they leave a module | A module's exception has its code in the module; it must not outlive it. |
| A lock next to the socket | A second service used to delete the first one's socket and paint alongside it. |
| `0600` socket in `/run/user/<uid>`, peer's uid checked | Only the user may pause or paint their lights; `$USER` is not trusted for a path. |
| Bounded waits everywhere | One thread serves the whole service: nothing may block it for long. |
| OpenRGB **SDK server** of OpenLinkHub (port 6743), not its REST API | REST `/api/color/all` answered "success" without changing the LEDs. The SDK server only shows devices whose **"OpenRGB Integration"** switch is on in the OpenLinkHub WebUI. |
| D-Bus `org.kde.PlasmaShell.wallpaper(0)` | Its `Image` is the picture on screen right now, including the current slide. |
| inotify on the slideshow folders | Plasma sends no signal when the slide changes, but plasmashell opens the next picture. |
| The service's own read of the picture is not a change | Otherwise each read would trigger the next. |
| Watching `plasma-org.kde.plasma.desktop-appletsrc` and `kdeglobals` | Choosing a wallpaper doesn't always send a signal; a light/dark switch can pick another picture of a package. |
| One inotify instance, one loop for inotify and both D-Buses | A thread costs a stack, a malloc arena and two wakeups per event. One loop: 2 threads instead of 5, half the wakeups in a burst, and no locks between events and use cases. |
| Pictures are never decoded at full size | Only ~100×100 pixels are looked at. libjpeg-turbo decodes at 1/2–1/8 size, PNG rows are averaged as libpng reads them (block by block, no division per pixel), libwebp scales while decoding. A 6656×3744 PNG used to take 246 MB for a moment; now well under 1 MB. A **progressive** JPEG still needs all its coefficients (about 50 MB for 4K, capped at 512 MB). |
| `release_free_memory()` after each picture and each unload | `malloc_trim` gives the memory back to the system. |
| The picture format comes from the file's content | Many wallpapers are PNGs named `.jpg`. |
| Drivers and decoders are separate `.so` files, loaded only while needed | Code built into a program would be in memory even when unused (the decoders were 1.2 MB and five libraries at startup). Loading and unloading takes under 0.1 ms. |
| Release builds use `-Os`, except the loops over pixels | The service's work is light; its code is what it costs. `-Os` halves it against `-O3`, and the colours come out the same. |
| toml++ compiled in (header-only) | Only the parsing used is kept: about 50 KB instead of a 250 KB library at startup. |
| Modules use plain C++ types, no C layer | They are always built and installed with the programs, and the build id refuses any other. |
| Drivers build messages with `cat()`, not `std::format` | `std::format` adds about 70 KB to every module that uses it. |
| One driver failing doesn't stop the others | Each driver's errors are kept apart; the others are painted, and the failed one is retried. |
| The motherboard is driven directly (`/dev/hidraw`), not through OpenRGB | OpenRGB would have to run all the time. `udev/70-wallpapercolor.rules` gives the logged-in user access. Nothing is saved to the board's flash. |

## Build and test

```sh
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure    # unit tests and the dependency check
python3 cmake/check_dependencies.py src       # the dependency check alone
build/src/wallpapercolor extract -p ~/Pictures/wallpapers/some.jpg
```

`./install.sh` does the same, then installs both programs, the driver modules and the picture
decoders, and restarts the service.

Checking builds, kept apart from `build/`:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DWPC_SANITIZE=address
cmake --build build-asan && build-asan/tests/wpc_tests      # memory, leaks, undefined behaviour
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DWPC_SANITIZE=thread
cmake --build build-tsan && setarch -R build-tsan/tests/wpc_tests -tce="damaged files*"  # races
```

ThreadSanitizer can't follow the `longjmp` libjpeg and libpng use to report a damaged file, so
the tests that make them do it are named "damaged files…" and run only under the address build.
`setarch -R` is for kernels whose address randomisation is wider than ThreadSanitizer's memory
map. The service itself runs the same way (`build-asan/src/wallpapercolord`, after `systemctl
--user stop wallpapercolord`).
