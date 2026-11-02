# WallpaperColor: rewrite from Rust to C++

Goal: the same two programs (`wallpapercolord`, `wallpapercolor`) doing exactly the same things,
written in plain, readable C++ that one person can maintain and extend.

Scope: everything in the Rust tree today, **including the uncommitted phase 4 work** (IPC socket,
`status`/`pause`/`resume`/`apply`, the TUI and the comment-preserving settings editor). That is
about 3,200 lines of Rust, tests included. Expect about the same in C++.

---

## 1. Verdict

Every feature can be ported with the same behaviour. A few things will not be **byte-for-byte**
identical. They are listed in §4, each with how we check it or why it does not matter. Only one
needs real care: **colour extraction must give the same colours**. It depends on three things:
the image decoders, the resize filter and floating-point details. A parity test over your 255
wallpapers settles it before the Rust code is deleted (phase 7).

---

## 2. Library choices (Rust → C++)

All of these are in the Arch/CachyOS repos except FTXUI. Each one was picked because its code
is easy to read, not because it is the most powerful.

| Job | Rust today | C++ choice | Why this one |
| --- | --- | --- | --- |
| Build | Cargo workspace | **CMake + Ninja** (`pacman -S cmake ninja`; neither is installed yet) | Most C++ projects use it, so it's the most useful one to learn. One `CMakeLists.txt` per folder. |
| Image decoding | `image` 0.25 (zune-jpeg, png, image-webp, gif, bmp) | **stb_image** (`stb` package; JPEG, PNG, GIF, BMP) + **libwebp** (installed) for WebP | A single header. Detects the format from the file's bytes, so a PNG named `.jpg` still loads. |
| Resize (96×96, triangle filter) | `image::imageops::resize` | **Our own ~100-line port** of `image` 0.25.10's `sample.rs` | It's the only way to get the same pixels. Libraries (stb_image_resize2…) use other kernels. |
| TOML config | `serde` + `toml` | **toml++** (`tomlplusplus`, header-only) | Clear API: `tbl["led"]["mode"].value_or("arc")`. |
| Comment-preserving edits (TUI) | `toml_edit` | **Our own small line editor** (`settings.cpp`) | No C++ TOML library keeps comments when it writes the file. See §4. |
| JSON (IPC) | `serde_json` | **nlohmann/json** (`nlohmann-json`), using `ordered_json` | The best-known C++ JSON library. `ordered_json` keeps fields in serde's order. |
| CLI parsing | `clap` | **CLI11** (`cli11`, header-only) | Subcommands, `--config` global option, `--arc`/`--single` exclusion, 1–3 colours. |
| D-Bus | `zbus` 5 (blocking) | **sdbus-c++** 2.x (`sdbus-cpp`) | A C++ wrapper over systemd's sd-bus (already installed). Reads `a{sv}` maps as a `std::map`. |
| inotify | `inotify` crate | **The Linux API directly** (`<sys/inotify.h>`) | Three calls: `inotify_init1`, `inotify_add_watch`, `read`. A wrapper would add nothing. |
| Threads + channel | `std::thread`, `mpsc` | `std::jthread` + **our own `Channel<T>`** (~40 lines: mutex, condition variable, `recv`, `recv_for`) | The standard library has no channel. A small one keeps the daemon's structure the same as now. |
| Unix socket, TCP | `std::os::unix::net`, `std::net` | **POSIX sockets** (`socket`, `connect`, `poll`), wrapped in small `Fd`/`Socket` RAII classes | They close themselves; same timeouts as now (2 s connect, 5 s read and write). |
| TUI | `ratatui` 0.30 + crossterm | **FTXUI** 6.x (CMake `FetchContent`, pinned tag; not in the repos) | Works like ratatui: you describe the screen every frame. Borders, bold/dim/reverse and 24-bit colour. |
| Tests | `cargo test` | **doctest** (`doctest`, header-only) + CTest | Tests look like Rust's: `TEST_CASE("hex round trip") { CHECK(...); }`. |

What we don't use: Boost, Qt, coroutines, heavy template code, or our own macros apart from
`log()`. ncurses is out too (24-bit colour is awkward there), and so are hand-written D-Bus
messages.

---

## 3. Layout and code style

```text
WallpaperColor/
├── CMakeLists.txt              project, C++23, warnings, flags (see "Floating point" below)
├── src/
│   ├── core/                   no I/O except reading the config and the picture
│   │   ├── color.hpp/.cpp      Rgb, Hsv, hex parse/format              (color.rs)
│   │   ├── arc.hpp/.cpp        gradient(), mix()                       (arc.rs)
│   │   ├── resize.hpp/.cpp     triangle resample, port of image 0.25   (new: was inside `image`)
│   │   ├── extract.hpp/.cpp    range cut, vividness, arc picking        (extract.rs)
│   │   ├── config.hpp/.cpp     Config structs, load / to_toml / gain()  (config.rs)
│   │   └── ipc.hpp/.cpp        Request/Response/Status, socket_path(), call()  (ipc.rs)
│   ├── openrgb/                client.hpp/.cpp, wire.hpp/.cpp          (wpc-openrgb)
│   ├── kde/                    plasma.hpp/.cpp                          (wpc-kde)
│   ├── util/                   channel.hpp, fd.hpp (RAII), log.hpp
│   ├── daemon/main.cpp         wallpapercolord                          (wallpapercolord)
│   └── client/                 main.cpp, settings.cpp, tui.cpp          (wallpapercolor)
├── tests/                      one test file per module, same cases as the Rust tests
├── tools/ upstream/ systemd/   unchanged
└── install.sh                  cargo → cmake; same install paths and service unit
```

Each Rust crate becomes a static library (`wpc_core`, `wpc_openrgb`, `wpc_kde`), so the same
rule as now holds: the daemon never links the TUI, and `core` never touches the network.

**Rules for readability** (these go in `CONTRIBUTING.md`):

- C++23, compiled with `-std=c++23` (not `gnu++23`) and `-Wall -Wextra -Wpedantic`.
- One `namespace wpc`. Structs with public fields for data (`struct Zone { std::string name; int kind; uint32_t leds; };`), classes only where something has to be cleaned up (sockets, D-Bus).
- **Errors:** functions that can fail throw `wpc::Error` (a `std::runtime_error`). Callers catch it exactly where the Rust code matches on `Err`. Values that may be missing use `std::optional`. This keeps the code short: with `std::expected`, every Rust `?` would become a three-line `if`.
- `std::filesystem::path` for paths, `std::format`/`std::print` for text, `std::chrono` for time.
- No `auto` where the type isn't obvious; no raw `new`/`delete`.
- Every function keeps the doc comment of its Rust original, so you can read the two side by side.

**Floating point:** the colour code uses `float` (Rust `f32`) and the config uses `double`
(Rust `f64`), as now. Compile with `-ffp-contract=off` so GCC cannot fuse `a*b+c` into one FMA
instruction (CachyOS builds for x86-64-v3, which has FMA). Rust never fuses them, and fused
results differ in the last bit. Never use `-ffast-math`. Rust's `rem_euclid`, `total_cmp` and
`clamp` become small helpers in `color.hpp` with tests.

**Rust behaviours that must be copied on purpose** (each gets a comment in the code):

| Rust | Careful in C++ |
| --- | --- |
| `sort_by` / `sort_by_key` are **stable** | `std::stable_sort`, never `std::sort` (equal vividness must keep palette order) |
| `max_by_key` returns the **last** of equal maxima | a hand-written loop with `>=` (in `range_cut`, `widest_channel`, `resolve_image`) |
| `Vec::swap_remove(i)` (moves the last box into slot `i`) | `boxes[i] = std::move(boxes.back()); boxes.pop_back();`: box order decides which equal-sized box sorts first |
| `f32::round` rounds halves away from zero | `std::round` does the same (not `std::nearbyint`) |
| `as u8` after `clamp` | explicit `static_cast<uint8_t>` after clamping |
| `String::from_utf8_lossy` on device names | replace invalid UTF-8 with U+FFFD (small helper) |

---

## 4. What cannot be exactly the same

| # | Difference | Severity | What we do |
| --- | --- | --- | --- |
| 1 | **JPEG pixels**: zune-jpeg and stb_image may decode some pixels ±1 apart (IDCT, chroma upsampling). PNG, GIF and BMP are lossless, so their pixels are identical. Your wallpapers are 103 JPEG and 152 PNG, no WebP and no 16-bit. | Can change a colour by a unit or two. In rare cases it could change which palette entry wins. | **Parity test** (phase 7): Rust vs C++ `extract` on all 255 wallpapers. Goal: identical output. If some JPEGs differ, try libjpeg-turbo (installed) and keep whichever decoder matches better. Any remaining difference gets written down, picture by picture. |
| 2 | **WebP** decoding (image-webp vs libwebp) | None of your wallpapers is WebP | Same parity test, with a few generated WebP files. |
| 3 | **16-bit PNGs** go to 8 bits differently: `image` rounds (`(v+128)/257`), stb_image cuts (`v>>8`) | None in your collection | Load 16-bit with `stbi_load_16` and round like `image`. One small function, with a test. |
| 4 | **Comment-preserving TOML edits.** `toml_edit` handles any TOML layout. Our line editor handles the normal one: `[led]` / `[extract]` tables and `[[calibration]]` entries with `gain = [r, g, b]` on one line. | Only matters if you write `led.mode = …` or `led = { mode = … }`, or split `gain` over several lines | The editor recognises the normal layout (what `wallpapercolor config` prints and what the README shows). For anything else it leaves the file untouched and the TUI says "edit this setting in the file". The three Rust tests are ported as they are. |
| 5 | **Error message wording**: toml parse errors, `--help` layout, CLI11 vs clap usage errors, `strerror` text (`Connection refused` vs Rust's `Connection refused (os error 111)`) | Cosmetic: same meaning, different words | We copy our own messages word for word. Library messages stay as the libraries write them. |
| 6 | **`wallpapercolor config` output**: `toml::to_string_pretty` formatting | Must stay valid, equivalent TOML | We write it by hand in the same order and format (`1.0` keeps its `.0`), and check it against the Rust output in phase 7. |
| 7 | **TUI drawing**: FTXUI and ratatui draw borders and wrap text with different code | Same content, keys, colours and layout. A cell or two may differ at the edges. | Side-by-side screenshots at 80×24 and full screen. |
| 8 | **Memory use**: sdbus-c++ starts its own thread, like zbus | Should stay around the current 5 MB and 0 % CPU when idle | Measured in phase 7. If it's clearly worse, see "Later" in §6. |

Nothing else differs. The things you can see (behaviour, files, socket path, JSON on the wire,
OpenRGB packets, systemd unit, install paths, config keys and defaults) all stay the same.

---

## 5. Phases

Same rule as the Rust plan: every phase ends with something that runs and is tested, and the next
one does not start until the exit criterion is met. The Rust code stays in the repo as the
reference until phase 7.

### Phase 0: freeze the reference

- Commit the current Rust phase 4 work (it is uncommitted), finish it or mark it WIP, and tag the
  result `rust-final`. That tag defines what "exactly the same" means.
- Record golden outputs with the Rust binaries in `tests/golden/`:
  - `extract -p` for all 255 wallpapers;
  - `config` for an empty file, a partial file and a calibration file;
  - the JSON for every `Request`/`Response`;
  - the bytes `set` sends to the fake OpenRGB server.
- `pacman -S cmake ninja stb tomlplusplus nlohmann-json cli11 sdbus-cpp doctest`.

**Exit:** the golden files are committed and an empty `CMakeLists.txt` builds a "hello" binary.

**Done so far (2026-11-02):**

- WIP committed and tagged `rust-final`. It builds. **50/51 Rust tests pass.** The failing one,
  `settings::tests::edits_keep_comments`, is a real WIP bug: `set_mode` and `set_intensity`
  drop the trailing comment on the line they rewrite (only `set_gain` keeps it). The C++
  version does what the test asks for (keeps the comment), so on this point it intentionally
  differs from the frozen Rust code.
- `tests/golden/extract.txt`: `extract -p` on all 255 wallpapers (relative names, default config).
- `tests/golden/config/*.toml` → `*.out`: the `config` output for empty, partial and
  calibration files, plus the error output for an unknown key and a bad `mode`.
- `tests/golden/ipc.txt`: the JSON of every request/response, an empty and a full `Status`,
  and how the service parses odd requests. Made by a throwaway program against the frozen
  `wpc-core`.
- `tests/golden/openrgb/`: output and every packet sent for `devices` and 6 `set` scenarios
  (one colour, arc, single, `--image tests/golden/gradient.png`, calibrated). Recorded with
  `tools/golden-openrgb.sh` against `tools/fake-openrgb.py`, a fake server laid out like your
  hardware. Run the same script on the C++ binary and `diff -r` the two folders.
- `CMakeLists.txt` (C++23, `-ffp-contract=off`, LTO) and a placeholder `src/client/main.cpp`.
  It compiles with g++ 16.
- Still to do: install the packages (needs sudo), then `cmake` builds the placeholder.

### Progress of phases 1–6 (2026-11-02)

All six are written and pass 58 unit tests (the 51 Rust ones plus 7 for Rust quirks the
port has to copy). What matches the Rust reference, checked against `tests/golden/`:

| Check | Result |
| --- | --- |
| `extract -p` on 152 PNG wallpapers | **identical** (the resize port is bit-exact) |
| `extract -p` on 103 JPEG wallpapers | 70 identical, **33 differ** (see "Open decisions" below) |
| `config` for the valid files | identical; error messages give the same line, column and text, without Rust's source snippet |
| `devices` + 6 `set` scenarios: output **and every packet** | identical |
| `kde_current` vs the Rust example | identical |
| Daemon scenario (status, pause, resume, apply, reload, config typo, OpenRGB down → backoff → back) | identical log and answers; 0 CPU ticks idle |
| TUI at 100×30, before and after a key press, and the saved config | identical text and file; only green/cyan are sent as ANSI codes instead of 256-colour indexes |

Not yet done: you watching the LEDs, a week of use, and phase 7 (switching `install.sh`, deleting Rust).

**Open decisions:**

1. **JPEG decoding.** zune-jpeg, the Rust decoder, has its own 14-bit colour conversion and its
   own chroma upsampling. No standard decoder does the same (stb_image: 2–10 % of bytes differ
   by up to 4/255; libjpeg-turbo is about as far off). 33 of the 103 JPEG wallpapers get a
   different colour: median hue shift 1°, but in 14 the main colour changes slightly, and in
   one (`yamasakiu-1.jpg`) a different third arc colour is picked. To match exactly, port
   zune-jpeg's IDCT, upsampling and colour conversion on top of libjpeg-turbo's decoded
   coefficients: about 400–600 lines in one `jpeg.cpp`.
2. **Retry bug in the Rust original.** While OpenLinkHub is unreachable, every retry re-reads the
   wallpaper. That *open* inside the watched slideshow folder fires inotify, which triggers the
   next retry 0.85 s later. The 2 → 60 s backoff never takes effect, and a JPEG is decoded about
   once a second until OpenLinkHub returns. Proposed fix: ignore inotify opens of the picture
   currently on screen (a new slide always opens a different file).
3. **Memory.** Peak while extracting a 4K JPEG: Rust 37 MB, C++ 61 MB (stb_image's buffer is
   copied once). Idle after a send: about 22–27 MB for both, memory malloc keeps after freeing.
   `malloc_trim(0)` after each extraction would bring it close to zero.

### Phase 1: `core`, colour and extraction

- `color`, `arc`, `resize`, `extract`, plus `wallpapercolor extract <img>… [-p]`.
- All 22 extraction tests, 3 colour tests and 5 gradient tests ported.

**Exit:** C++ `extract -p` matches the golden output on all 255 wallpapers, or every difference
is listed and explained by #1 in §4.

### Phase 2: config

- `config.hpp/.cpp` with toml++: defaults, partial files, an error on unknown keys (toml++ allows them, so we check by hand to copy `deny_unknown_fields`), `gain()` matching, `to_toml()`.
- `wallpapercolor config`; the 7 config tests.

**Exit:** `config` output matches the golden files; a misspelled key is rejected.

### Phase 3: OpenRGB client

- `wire` (bounds-checked `Reader`, so a truncated payload is an error and never a crash) and `client` (connect with 2 s timeout, version negotiation up to v4, `DEVICE_LIST_UPDATED` skipped, `UPDATE_LEDS`, `paint_calibrated`).
- The fake server and its 7 tests ported; `wallpapercolor devices` and `set`.

**Exit:** the bytes sent match the golden capture, and **you watch the RAM, AIO and fans change**
with `wallpapercolor set ff0000` and `set --image`.

### Phase 4: KDE source

- `plasma.cpp` with sdbus-c++: `wallpaper(0)`, the `wallpaperChanged` and `NameOwnerChanged` match rules, `url_to_path`, `resolve_image`, `dark_mode`; the 3 tests.

**Exit:** a small `examples/current` prints the same thing as the Rust example.

### Phase 5: the service

- `util/channel.hpp`; inotify reader thread, D-Bus listener, IPC socket thread; `Daemon` with
  the same `classify` / `sync` / `fail` / `watch_slides`; 800 ms settle, 2–60 s backoff, the
  cache key, pause/resume/apply.
- `wallpapercolor status/pause/resume/apply`.

**Exit:** it follows the slideshow, manual wallpaper changes, settings changes, a light/dark
switch and config edits in about 1 s. Idle: 0 CPU ticks in 30 s. RAM measured and noted.

### Phase 6: TUI and settings editor

- `settings.cpp` (line editor, atomic save through `config.toml.tmp` + rename) and its 3 tests.
- `tui.cpp` with FTXUI: header, device previews with calibrated colours, settings list, the same
  keys (↑↓/jk, ←→/hl, p, a, q/Esc), 1 s refresh from a ticker thread that exists only in the TUI.

**Exit:** everything that can be changed in the Rust TUI can be changed in the C++ one, with the
same result in the TOML file.

### Phase 7: parity and switch-over

- Run every golden comparison again and measure CPU and RAM against the Rust daemon.
- Switch `install.sh` to CMake and reinstall. The systemd unit doesn't change.
- Delete `crates/`, `Cargo.*` and `target/`, and update `README.md` and `docs/PLAN.md`
  (the "Decisiones técnicas" section).

**Exit:** a week of normal use with no difference you can notice. The `rust-final` tag stays
in git history.

---

## 6. Later (not part of the rewrite)

- Fold the daemon's threads into one `poll()` loop over the inotify, sd-bus and socket file
  descriptors. That was the original plan's goal, and it's easier in C++ than it was with zbus.
  It changes no behaviour, so it waits until parity is proven.
- The rest of the Rust plan's phase 5 (multi-zone palettes, fades, GNOME/Hyprland sources,
  real OpenRGB sink) continues on the C++ code.
