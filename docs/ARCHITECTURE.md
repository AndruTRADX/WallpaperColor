# How WallpaperColor is built

This document is the rulebook for the code. Read **The layers** before writing anything: every
file in `src/` follows it, and `ctest` fails when a file doesn't.

## The parts

- **`wallpapercolord`**, the service (`src/daemon/`): runs in the background as a systemd user
  unit. It finds the picture on screen, takes its colours and paints the RGB hardware with them.
- **`wallpapercolor`**, the terminal (`src/terminal/`): the terminal UI and the commands
  (`status`, `set`, `devices`, `drivers`, `extract`, `config`, `pause`, `resume`, `apply`).
  The service works without it.
- **The drivers** (`src/drivers/`): one module (`wpc-driver-<name>.so`) per kind of hardware,
  loaded by the programs only while it is needed.
- **`shared/`**: what both programs must do the same way (the colours, the settings file,
  reading pictures, the protocol between them).
- **`utils/`**: plain code that knows nothing about WallpaperColor.
- **The picture decoders** (`src/utils/picture_decoding.cpp`): their own module,
  `wpc-pictures.so`, with the image libraries; `shared/`'s `picture_files` loads it only while
  it reads a picture.

The terminal and the service talk over a Unix socket (`$XDG_RUNTIME_DIR/wallpapercolor.sock`):
one JSON line in, one JSON line out. Settings never go through the socket: the terminal edits
the config file, and the service notices and reloads it.

## The layers

The service, the terminal and every driver are each built from the same layers. `shared/` uses
the same names for what it has (`domain/`, `models/`, `infrastructure/services/`).

```text
<part>/
  domain/                                   entities and their business rules
  application/
    features/<feature>/<feature>.{hpp,cpp}  running one feature: orchestrating services
    contracts/<contract>.hpp                interfaces to this part's services
    contracts/external/<contract>.hpp       interfaces to another module's services
    events/<event>.{hpp,cpp}                listening, and notifying that something happened
    models/<model>.hpp                      the structure of what the application uses
  infrastructure/
    services/<service>.{hpp,cpp}            services: one concrete job per method
    external/<adapter>.hpp                  another module's service behind an external contract
  presentation/<presentation>.{hpp,cpp}     showing features, and running them
  main.cpp (module.cpp for a driver)        wiring: one implementation of each contract
```

Nothing else goes in a part: no `common/`, no `helpers/`, no file loose in `application/`.
What would be "common" is either a utility (`utils/`), a rule of the domain, or a model.

### `application/features/`: running one feature

`features/<feature>/<feature>.hpp` and `<feature>.cpp`. A feature runs one use case: paint the
wallpaper, apply the settings, list the devices. It may use no service, one, or several, and
**always through contracts**, never through a service's class.

**The feature is where orchestration lives.** Whenever doing something takes more than one
service method, the feature calls each method and joins their results:

- two services: the feature asks one, then hands the answer to the other;
- two methods of the same service: the feature calls both, in order (`open()`, then
  `controllers()`), never a third method that calls the other two;
- a decision between calls (retry or not, which driver is needed, what a failure means, when
  to open and close a connection): the feature makes it, using the domain's rules.

```cpp
// features/paint_wallpaper: four services, joined by the feature.
WallpaperSettings on = desktop.current();                             // DesktopWallpaper
std::optional<fs::path> picture = packages.picture(*on.image,         // WallpaperPackages
                                                   scheme.is_dark()); // ColourScheme
Picture read = pictures.read(*picture, picture_side(config.extract)); // Pictures
std::vector<Rgb> stops = extract(read, config.extract).stops(mode);  // domain
```

A feature takes only the contracts and models it uses (not the whole `ServiceParts`), so its
header says what it touches and a test fakes only that. Logic two features share stays in the
feature it belongs to, and the other one calls it there (the white test paints through
`paint_wallpaper`'s `paint_colours`).

### `application/contracts/`: interfaces only

`contracts/<contract>.hpp`. A contract is an **interface to a service**: an abstract class with
only `virtual … = 0` methods. No code, no function bodies, no data structures (those are
models), no constants.

- Each method has **one specific job**, with low coupling: `is_dark()`, `read(path)`,
  `show(driver, device, zones)`. A contract never offers a method that does the work of two
  (that would push orchestration into the service).
- Contracts are flat: `contracts/<contract>.hpp`, no subject folders.
- **A service of another module** (`shared/`, `drivers/host`, or the other program over the
  socket) has its contract in **`contracts/external/<contract>.hpp`**, so a reader sees at once
  what leaves the part.

A contract lives where it is used: the service's `contracts/external/driver_modules.hpp` and
the terminal's are separate files, each with only what its program needs.

### `application/events/`: listening and notifying

`events/<event>.hpp` and `<event>.cpp`. An event **listens for something happening** (through
contracts: `FileWatch`, `DesktopSignals`, `RequestSocket`) and **sends a notification that it
happened**. It doesn't run features or decide what happens next: the presentation receives the
notification and does that.

The service's events are `desktop_changed` (Plasma's signals, its settings files, a slideshow
folder), `settings_changed` (the config file), `hardware_changed` (waking from sleep, a device
plugged in) and `request_received` (a line from the terminal).
The notifications they send are a model (`models/notifications.hpp`).

### `application/models/`: the structure of entities

`models/<model>.hpp`. A model defines the structure of something the application uses: its
state (`ServiceState`), what a service takes or returns (`WallpaperSettings`, `FileChange`,
`DriverModule`), what a feature returns, the notifications, and the parts each program is wired
with (`ServiceParts`, `TerminalParts`). Models hold data; a model's method is at most a tiny
question about its own data (`DriverModule::paints(c)`).

### `domain/`: entities and their business rules

`domain/<domain>.{hpp,cpp}`. The entities and how they behave: which zone of a device gets
which colour (`outputs`), how a fade goes (`fade`), how long to wait before retrying (`retry`),
which drivers the chosen components need (`driver_choice`), which files are pictures
(`picture_names`), how colours are picked from a picture (`shared/domain/colours/extract`).
**Pure**: no files, no sockets, no clocks, no contracts. A domain test needs no fake.

### `infrastructure/services/`: one concrete job per method

`services/<service>.hpp` and `<service>.cpp`. A service is a library of specific functions,
exposed through a contract: a `final` class implementing it. This is where the outside world
is touched: D-Bus, files, sockets, `/dev/hidraw`, inotify, `dlopen`, the clock, `malloc_trim`.

The two rules of services are absolute.

**1. Every method follows the Single Responsibility Principle, strictly.** One method does one
concrete job, nameable in a few words, and nothing else. A method that does "this and then
that" is two methods, and a feature calls both. A method does too much when:

- its name needs an "and" (`load_and_paint`), or a flag picks what it does;
- it decides something: whether to retry, what an error means, when to reconnect;
- it waits, loops over frames or keeps a schedule (the fade is a feature; the service shows
  one frame);
- testing it would need a second contract.

A service is still one class with several methods: `DriverModules` has `installed()`,
`load()`, `unload()`, `devices()`, `show()`, each one job about the same thing outside. If a
service needs state (a connection, the loaded drivers), it keeps it and offers methods to open
and close it; **when** to open and close is the feature's choice.

**2. A service never calls another service. Never.** Not to work, not to finish its job, not
once. A service's methods use only the service's own state, utilities (`utils/`) and libraries.
If the job needs another service, the job is a feature: the feature calls this service, then
the other one, and orchestrates them.

```cpp
// Wrong: a service calling another service.
std::optional<fs::path> PlasmaPackages::picture(const fs::path& image) {
    bool dark = scheme_.is_dark();  // ColourScheme, inside WallpaperPackages
    ...
}

// Right: each service does its own job; the feature joins them.
std::optional<fs::path> picture = packages.picture(*on.image, scheme.is_dark());
```

Using a utility is not using a service (see **Services and utilities** below).

### `infrastructure/external/`: adapters

`external/<adapter>.hpp`. An adapter puts **exactly one** service of another module behind
this part's external contract, forwarding each method to it and adding nothing:
`daemon/infrastructure/external/pictures.hpp` puts `shared/infrastructure/services/picture_files`
behind the service's `contracts/external/pictures.hpp`. An adapter is not an exception to the
rules of services: one service in, no logic, no second service.

### `presentation/`: showing features, and running them

`presentation/<presentation>.{hpp,cpp}`. The presentation shows what features return and runs
features: a command, a key in the terminal UI, a request from the terminal, a notification from
an event. It has the logic of **how information is shown** (layout, colours, wording, which
page is open, a key pressed twice to confirm) and nothing else.

It **never** has application logic, and it never uses services, contracts, libraries other than
its UI toolkit, or utilities. It includes only features, models, the service's events, and the
entities' types to show them (`shared/domain/`, `shared/models/`).

When a presentation file needs an `if` about the data rather than about how to show it, that
`if` belongs in a feature. Examples from the code: the terminal UI's device preview is the
feature `preview_devices`; the Drivers page's "loaded / error" next to each component comes
ready from `choose_components`; `set` gets `painted` and `not_painted` from `set_colours`
instead of looking at the text.

The service's presentation is `service_loop` (it receives the notifications, merges a burst,
waits for a retry and runs the features) and `requests` (a request → its feature → the
answer). A driver's presentation is its `Driver`, which only runs the driver's features.

### `main.cpp` and `module.cpp`: wiring

The composition root: it creates one implementation of each contract and connects them. No
logic beyond reading the command line. A driver's `module.cpp` reads its settings and wires the
real service into its `Driver`.

### Services and utilities

| | A service method | A utility |
| --- | --- | --- |
| What | A job **of the application** that touches the outside or keeps state | Plain code that knows nothing about WallpaperColor and keeps no state |
| Examples | ask Plasma for the wallpaper, load a driver, read the settings file, show a frame | wrap a socket, inotify, epoll or `/dev/hidraw`; parse UTF-8, a URL, TOML text; `cat()`; the log; the queues |
| Reached | through a contract, so a feature can be tested with a fake | directly, by `#include "utils/…"` |
| Where | `infrastructure/services/` | `utils/`, even if one part uses it |

A utility attacks one very specific code problem (validate a string, read a file descriptor),
never a function of the application such as changing the lights. A service may use utilities;
that is not a service using a service.

## Where things go

Grouped by **who uses it**: everything the service uses is under `daemon/`, everything the
terminal uses under `terminal/`, each driver under its own folder; only what both programs share
is in `shared/`.

```text
src/
  daemon/                                   wallpapercolord
    main.cpp
    domain/                                 outputs, fade, retry, driver_choice, picture_names
    application/
      features/                             paint_wallpaper (with paint_colours, schedule_retry,
                                            time_to_retry), apply_settings, white_test, pause,
                                            report_status, exchange_messages
      contracts/                            desktop_wallpaper, desktop_signals,
                                            wallpaper_packages, colour_scheme, file_watch, clock,
                                            memory, request_socket, system_signals
      contracts/external/                   driver_modules (drivers/host), pictures,
                                            settings_source, message_codec (shared/)
      events/                               desktop_changed, settings_changed, hardware_changed,
                                            request_received
      models/                               service_state, service_parts, notifications,
                                            driver_module, wallpaper_settings, file_change
    infrastructure/
      services/                             plasma_shell (D-Bus), wallpaper_packages,
                                            colour_scheme (kdeglobals), file_watch (inotify),
                                            clock, memory (malloc_trim), request_socket,
                                            logind (system D-Bus)
      external/                             driver_modules, pictures, settings_source,
                                            message_codec
    presentation/
      service_loop/                         notifications → features
      requests/                             a request → its feature → the answer

  terminal/                                 wallpapercolor
    main.cpp                                the command line and the wiring
    application/
      features/                             load_settings, show_settings, edit_settings,
                                            save_settings, service_status, control_service,
                                            extract_colours, list_devices, detect_hardware,
                                            set_colours, choose_components, driver_options,
                                            preview_devices
      contracts/                            settings_editor
      contracts/external/                   service_socket, message_codec, driver_modules,
                                            pictures, settings_file
      models/                               terminal_parts, driver_module
    infrastructure/
      services/                             settings_editor (config.toml, keeping comments),
                                            service_socket (the socket's client side)
      external/                             driver_modules, pictures, settings_file,
                                            message_codec
    presentation/
      cli/                                  one file per command, and output (printing)
      tui/                                  app (loop, shared keys), home_page, drivers_page,
                                            widgets

  drivers/
    sdk/                                    what a module is written against: Driver,
                                            DriverInfo, Component, Zone, Device, DriverSettings
    host/services/                          module_catalog: finding, loading, creating drivers
    openrgb/                                an OpenRGB SDK server (OpenLinkHub's or OpenRGB's)
    fusion2/                                the Gigabyte RGB Fusion 2 board, over /dev/hidraw
    razer/                                  the Razer Huntsman Mini keyboard, over /dev/hidraw
      domain/                               settings (its config table), zones
      application/contracts/                the protocol's service (with open, close, is_open)
      application/models/                   what the driver keeps between calls
      application/features/                 list_devices, show_frame
      infrastructure/services/              the protocol
      presentation/driver.hpp               the Driver the programs see: runs the features
      module.cpp                            what the module exports, with the real service

  shared/
    domain/colours/                         colour, gradient, picture, extract, paint
                                            (Painter), calibration
    domain/settings/                        the settings, in parts, and their rules
    models/protocol/                        the messages between terminal and service
    infrastructure/services/                settings_file (toml++), picture_files (loads the
                                            decoders' module), protocol_json (nlohmann/json),
                                            socket_path

  utils/                                    error, text (cat), log, channel (the thread queue),
                                            event_loop (epoll), event_queue (its queue), socket,
                                            inotify, hidraw, utf8, url, toml (TOML text),
                                            picture_decoding (libjpeg-turbo, libpng, libwebp,
                                            stb: built as wpc-pictures.so, not into the programs)
```

Each program's types that have the same name in the other (`DriverModules`, `DriverModule`,
`Pictures`, `MessageCodec` and their adapters) are in an inline namespace, `wpc::daemon` or
`wpc::terminal`. Each program just writes `DriverModules`; the tests, which link both, write
`wpc::daemon::HostDriverModules`.

### Who may include what

| Folder | May include |
| --- | --- |
| `utils/` | `utils/` |
| `shared/domain/` | itself, `drivers/sdk/`, `utils/` |
| `shared/models/` | itself, `shared/domain/`, `drivers/sdk/`, `utils/` |
| `shared/infrastructure/services/` | its own header, `shared/domain/`, `shared/models/`, `drivers/sdk/`, `utils/` |
| `drivers/sdk/` | itself, `shared/domain/colours/color.hpp` (the `Rgb` type), `utils/` |
| `drivers/host/services/` | its own header, `drivers/sdk/`, `utils/` |
| `<part>/domain/` | itself, `shared/domain/`, `shared/models/`, `drivers/sdk/`, `utils/` |
| `<part>/application/contracts/` | itself, its models, its domain, `shared/domain/`, `shared/models/`, `drivers/sdk/`, `utils/` |
| `<part>/application/models/` | itself, its contracts, its domain, `shared/domain/`, `shared/models/`, `drivers/sdk/`, `utils/` |
| `<part>/application/features/` | features, contracts, models, its domain, `shared/domain/`, `shared/models/`, `drivers/sdk/`, `utils/` |
| `<part>/application/events/` | events, contracts, models, its domain, `shared/domain/`, `shared/models/`, `drivers/sdk/`, `utils/` |
| `<part>/infrastructure/services/` | **its own header** (no other service), contracts, models, its domain, `shared/domain/`, `shared/models/`, `drivers/sdk/`, `utils/` |
| `<part>/infrastructure/external/` | its own header, `contracts/external/`, models, **one** service of `shared/infrastructure/services/` or `drivers/host/services/`, `shared/domain/`, `shared/models/`, `drivers/sdk/`, `utils/` |
| `<part>/presentation/` | itself, features, models, the service's events, `shared/domain/`, `shared/models/` (a driver's: `drivers/sdk/` instead of `shared/`) |
| `<program>/main.cpp` | its own program, `shared/`, `drivers/sdk/`, `drivers/host/`, `utils/` |
| `drivers/<name>/module.cpp` | its own driver, `drivers/sdk/`, `utils/` |

For a driver, `shared/` is replaced by `drivers/sdk/` and `utils/`: a driver never includes a
program, `shared/` or another driver.

### What checks it

`ctest` runs two checks on every build:

- **"layers hold and include only what they may"**, `python3 cmake/check_layers.py src`.
  It fails on:
  - any `#include` the table above doesn't allow;
  - a contract with code, a function body or a data structure;
  - a service including another service;
  - an adapter wrapping more or less than one service;
  - a folder that isn't a layer: a part holds only `domain/`, `application/`,
    `infrastructure/` and `presentation/` (plus `main.cpp` or `module.cpp`); `application/`
    only `contracts/` (flat, plus `external/`), `features/<feature>/<feature>.*`, `events/` and
    `models/`; `infrastructure/` only `services/` and `external/`.
- **"every file is where its users are"**, `python3 cmake/usage_map.py src --check`. It follows
  the `#include`s from each `main.cpp` and `module.cpp`, and fails when a file in `shared/` is
  used by only one program or a program's file is reached by the other.
  `python3 cmake/usage_map.py src` prints who uses every file.

What no script can check (one job per service method, no application logic in presentation)
is checked when writing and reviewing code, with the tests of **The layers**.

Each folder is also its own CMake library (`src/CMakeLists.txt`), linking only what it may use:
the features link no third-party library, the terminal links no socket server or file watcher, the
service no socket client, settings editor or terminal UI.

## How to add something

### Decide what each piece is

| The code… | is… | goes in |
| --- | --- | --- |
| is a rule about the entities, with no I/O | domain | `domain/` |
| touches the outside or keeps a connection (D-Bus, a file, a socket, a device, the clock) | a service, behind a contract | `application/contracts/` + `infrastructure/services/` |
| is a service of another module | an adapter, behind an external contract | `application/contracts/external/` + `infrastructure/external/` |
| calls more than one service method, or decides between calls | a feature | `application/features/<feature>/` |
| notices that something happened and says so | an event | `application/events/` |
| is the shape of something the application keeps, passes or returns | a model | `application/models/` |
| shows a result, or turns a command, key or request into a feature call | presentation | `presentation/` |
| is plain code that knows nothing about WallpaperColor | a utility | `utils/` |

### A feature

1. `application/features/<feature>/<feature>.hpp`: a comment saying what it does and what a
   failure does, then the function(s), taking only the contracts and models used:

   ```cpp
   // Feature: <what it does, in one sentence>. <What happens on a failure.>
   #pragma once
   #include "daemon/application/contracts/clock.hpp"
   #include "daemon/application/models/service_state.hpp"
   namespace wpc {
   void dim_at_night(ServiceState& state, Clock& clock);
   }
   ```

2. `<feature>.cpp`: the orchestration. Call each service method through its contract, in
   order, pass results from one to the next, apply the domain's rules, record the outcome in a
   model or return it.
3. Run it from the presentation: `daemon/presentation/service_loop` (on a notification) or
   `requests` (on a request); `terminal/presentation/cli/<command>.cpp` or a page in
   `terminal/presentation/tui/`.
4. Test it with a fake of each contract it uses (`tests/test_service.cpp`,
   `tests/test_terminal.cpp`): no hardware, no files, no D-Bus.

### A service

1. The contract, `application/contracts/<contract>.hpp` (or `contracts/external/`): only
   `virtual … = 0` methods, one job each. Data it takes or returns goes in a model.
2. The service, `infrastructure/services/<service>.{hpp,cpp}`: a `final` class implementing it.
   One concrete job per method. Utilities and libraries only; never another service.
3. Wire it in `main.cpp` (or `module.cpp`), and add it to `ServiceParts` or `TerminalParts` if
   the presentation hands it to a feature.
4. If a feature now needs this service and another one, the feature calls both.

For a service of another module: its external contract, and an adapter in
`infrastructure/external/` forwarding to exactly that service.

### An event

`application/events/<event>.{hpp,cpp}`: a class whose `start()` listens through contracts and
calls `notify_` with a notice. Add the notice to `models/notifications.hpp`, its case to
`presentation/service_loop` (which features it runs), and start the event in `daemon/main.cpp`.

### Common cases

- **A command of the terminal**: a feature, then `presentation/cli/<command>.cpp` printing its
  result, declared in `presentation/cli/commands.hpp`, parsed in `terminal/main.cpp`.
- **A request from the terminal to the service**: a `Request` in `shared/models/protocol/`, its
  JSON in `shared/infrastructure/services/protocol_json.cpp`, its case in
  `daemon/presentation/requests/requests.cpp` running its feature.
- **A setting**: its field and rules in `shared/domain/settings/`, read and written in
  `shared/infrastructure/services/settings_file.cpp`; for the terminal UI, a `NumberSetting` in
  `edit_settings` and its label in `presentation/tui/home_page.cpp`.
- **Another desktop** (GNOME, Hyprland…): services in `daemon/infrastructure/services/`
  implementing `DesktopWallpaper`, `DesktopSignals`, `WallpaperPackages` and `ColourScheme`,
  picked in `daemon/main.cpp`. Nothing else changes.
- **Other hardware**: a new driver module, with the same layers:
  [ADDING-A-MODULE.md](ADDING-A-MODULE.md).
- **Another way to spread colours over the LEDs**: a `Mode` and its case in
  `shared/domain/colours/paint.cpp`; the service, `set` and the preview all paint through it.
- **Another way to pick colours**: `shared/domain/colours/extract.cpp`;
  `wallpapercolor extract -p <picture>` shows the result and its palette.

## How it works

### From wallpaper to LEDs

```text
 slideshow folder: a picture opened ──┐
 kdeglobals, Plasma's settings file ──┤  events/desktop_changed
 Plasma's D-Bus signals ──────────────┤
 config.toml written ─────────────────┤  events/settings_changed
 waking from sleep, a device added ───┤  events/hardware_changed
 a request from the terminal ─────────┤  events/request_received
                                      └─▶ notifications ─▶ presentation/service_loop
                                                                   │
   features/paint_wallpaper                                         ▼
   1. what is on screen                             DesktopWallpaper
   2. a wallpaper package → its picture             WallpaperPackages, ColourScheme
   3. same picture and settings as last time? stop  Pictures.modified_time
   4. read it small, pick its colours               Pictures.read, domain extract
   5. every LED's colour (arc / single / split)     domain outputs, Painter
   6. fade the chosen zones of every loaded driver  DriverModules.show, Clock, domain fade
   7. give the memory back                          Memory.release
```

Every notification is only a **trigger**: the answer always comes from the desktop (step 1),
so a false alarm costs one D-Bus call and changes nothing. A burst (Plasma rescanning a folder
opens every file in it) is merged: the loop waits until 0.3 s pass with no new notification.
The lights start fading about 0.32 s after Plasma opens the next slide (measured).
Requests are answered at once; one that waits more than 2 s (during a fade) gets "service busy".

A fade sends 25 frames a second, blending each LED from what was sent last to its new colour,
for `[led] fade` seconds (at most 1.5). If a driver or Plasma doesn't answer, the service
retries after 2 s, 4 s, 8 s … up to 60 s, and paints the other drivers meanwhile. While nothing
happens, both threads sleep in the kernel: 0 % CPU, about 2.5 MB of memory of its own.

### Two threads

The service runs on **one loop** (`utils/event_loop`, epoll) and one more thread:

- **The loop's thread** (`main`) waits in one `epoll_wait` for everything the events hear: the
  inotify fd (`file_watch`), the session bus (`plasma_shell`) and the system bus (`logind`),
  whose messages sd-bus reads and dispatches on this thread (no `enterEventLoopAsync`). The
  retry and the 0.3 s of a burst are that wait's timeout. Every event's callback and every
  feature run here, one at a time: what they share needs no lock.
- **The request socket's thread** accepts the terminal's connections and waits for the loop's
  answer. It is a thread of its own so a request still gets "service busy" after 2 s while the
  loop is in a fade or waiting for Plasma. It hands the request over through `Notifications`
  (`utils/event_queue`), whose `send()` wakes the loop (an eventfd).

A source that fails (inotify, a D-Bus connection lost) ends the service with the reason in the
log, and systemd starts it again (`Restart=on-failure`): nothing goes deaf silently (R5).

### Rules that keep the service from going deaf

The service only wakes up for a notification or a due retry. A branch that leaves neither
armed puts it to sleep for good: the lights stay on an old wallpaper until a manual `apply`.
Every change to the loop, the events or the features keeps these, and `tests/test_service.cpp`
checks them over time (the `R1`… tests drive `ServiceLoop::step()` with a fake clock):

| Rule | What it means |
| --- | --- |
| R1 | While the service doesn't know what is on screen (Plasma unreachable, no screen), a retry or a signal is armed to wake it. |
| R2 | A due retry is always run and either cleared or moved on; it never stays due (the loop would spin). |
| R3 | The folder watches match the folders on disk: folders created or removed under a slideshow folder too. |
| R4 | Hardware that comes back (plugged in again, after a suspend) is painted again without waiting for the next wallpaper. Not heard yet: OpenLinkHub restarting (its server sends nothing to a client it no longer has). |
| R5 | What hears events doesn't stop silently: it starts again, or the service says so and checks everything. |

A test marked `doctest::should_fail()` records a known break of a rule; it is unmarked with
the fix.

### Drivers are modules

The code that talks to hardware is in driver modules, one file each: `wpc-driver-openrgb.so`
(OpenLinkHub: RAM, pump, fans), `wpc-driver-fusion2.so` (the Gigabyte board and its ARGB
headers) and `wpc-driver-razer.so` (the Razer Huntsman Mini keyboard). Neither program links them; both load them through `drivers/host` (behind their
`contracts/external/driver_modules.hpp`) from `~/.local/lib/wallpapercolor/drivers/`:

- the **service** keeps a module loaded only while one of its components (`[components]`,
  ticked on the terminal's Drivers page) is chosen. On every settings reload, `apply_settings`
  loads what is newly needed and unloads the rest; a module whose settings didn't change stays
  loaded, with its connection;
- the **terminal** loads them for `devices`, `set` and the Drivers page, and unloads them at
  once.

Knowing what a module paints means loading it: `drivers/host` loads each file once, the first
time it sees it, reads its `DriverInfo` and unloads it at once. Later calls (every settings
reload of the service) answer from what was read, without loading anything, unless the file
changed (a new `install.sh`). A driver nobody chose is in memory only for that first look.

Every driver implements `Driver` (`drivers/sdk/driver.hpp`): "which devices and zones do you
have" and "show these colours on these zones". The programs paint zones, never a brand's
devices. Each zone says what it is (`Component`: fans, pump, RAM…) and its shape (ring, line,
spot); the user can correct it with `[[component]]`.

Modules and programs share C++ types, so they must come from the same build: each module
carries the build's `WPC_BUILD_ID`, and a stale one is refused rather than crashing.
`install.sh` replaces them all together.

## Why it is built this way

| Choice | Reason |
| --- | --- |
| Layers, with services only behind contracts | Every feature can be tested with fakes, and a service can be replaced (another desktop, another protocol) without touching the features. |
| Services never call services; features orchestrate | Each service stays small, testable alone and replaceable; the order of things is in one place, the feature. |
| Folders by who uses the code | Opening `terminal/` or `daemon/` shows everything that part uses, and a change for one program can't reach the other. |
| `shared/` instead of copies | Colour extraction, pictures, the settings file and the protocol must be the same in both programs. |
| No dispatcher (like MediatR) | A feature is a function, called directly from the presentation; a generic layer would add code, not remove coupling. |
| The terminal loads drivers itself | `set` works without the service, and looking at hardware doesn't make the service load drivers nobody chose. |
| OpenRGB **SDK server** of OpenLinkHub (port 6743), not its REST API | REST `/api/color/all` answered "success" without changing the LEDs. The SDK server only shows devices whose **"OpenRGB Integration"** switch is on in the OpenLinkHub WebUI. |
| D-Bus `org.kde.PlasmaShell.wallpaper(0)` | Its `Image` is the picture on screen right now, including the current slide. |
| inotify on the slideshow folders | Plasma sends no signal when the slide changes, but plasmashell opens the next picture. |
| `desktop_changed` ignores opens of the picture on screen | The service opens it itself to read it; otherwise each read would trigger the next. |
| Watching `plasma-org.kde.plasma.desktop-appletsrc` and `kdeglobals` | Choosing a wallpaper doesn't always send a signal; a light/dark switch can pick another picture of a package. |
| One `file_watch` service (one inotify instance) | The settings file and KDE's files both live in `~/.config`; one inotify fd serves every watch. |
| One loop for inotify and both D-Buses, not a thread each | A thread costs a stack, a malloc arena and two wakeups per event (its own, then the loop's). With one loop: 2 threads instead of 5, about 50 KB less private memory and 220 MB less address space, half the wakeups in a burst, and no locks between events and features. |
| Pictures are never decoded at full size | Only ~100×100 pixels are looked at. libjpeg-turbo decodes at 1/2–1/8 size, PNG rows are averaged as libpng reads them, libwebp scales while decoding. A 6656×3744 PNG used to take 246 MB for a moment; now well under 1 MB. The exception is a **progressive** JPEG: libjpeg must keep all its coefficients even when it decodes at 1/8, so a 3840×2160 one still peaks around 50 MB, for a moment, and the memory is given back after. |
| `Memory.release()` (`malloc_trim`) after each picture and each unload | It gives the memory back to the system. |
| The picture format comes from the file's content | Many wallpapers are PNGs named `.jpg`. |
| Drivers are separate `.so` files, loaded only while needed | A driver built into a program would be in memory even when switched off. |
| The picture decoders are a separate `.so` (`wpc-pictures.so`), loaded only while a picture is read | The decoders and libjpeg, libpng, zlib and libwebp were about 1.2 MB of the service's memory and five of the fourteen libraries it loaded at startup, for a few seconds of work a day. Loading and unloading them takes under 0.1 ms, next to tens of milliseconds for the picture. |
| Release builds use `-Os`, except the loops over pixels (the decoders' module `-O3`, the colour maths `-O2`) | The service's work is light; its code is what it costs. `-Os` halves it against `-O3`, and the colours come out the same. |
| toml++ compiled in (header-only), not the system's `libtomlplusplus.so` | Only the parsing used is kept: about 50 KB instead of a 250 KB library loaded at startup. |
| Modules use plain C++ types, no C layer | They are always built and installed with the programs, and the build id refuses any other. |
| Drivers build messages with `cat()`, not `std::format` | `std::format` adds about 70 KB to every module that uses it. |
| One driver failing doesn't stop the others | Each driver's errors are kept apart; the others are painted, and the failed one is retried. |
| The motherboard is driven directly (`/dev/hidraw`), not through OpenRGB | OpenRGB would have to run all the time. `udev/70-wallpapercolor.rules` gives the logged-in user access. Nothing is saved to the board's flash. |

How the colours are picked (palette, ignoring grey, specks and skin, pastels, the three "arc"
colours) is explained at the top of `src/shared/domain/colours/extract.hpp`.

Conventions: functions that can fail throw `wpc::Error` with a message ready for the user;
values that may be missing are `std::optional`; colour maths uses `float`.

## Build and test

```sh
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure   # unit tests and the two layer checks
python3 cmake/check_layers.py src            # the layers alone
python3 cmake/usage_map.py src               # who uses every file
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

ThreadSanitizer can't follow the `longjmp` libjpeg and libpng use to report a damaged file,
so that one test runs only under the address build. `setarch -R` is for kernels whose address
randomisation is wider than ThreadSanitizer's memory map. The service itself runs the same way
(`build-asan/src/wallpapercolord`, after `systemctl --user stop wallpapercolord`).
