# Adding a driver module

A **module** is one **driver** built as its own file, `wpc-driver-<name>.so`. A driver speaks
one protocol and paints some **components** (fans, pump, RAM, motherboard, ARGB, keyboard…).
Every module is installed, but the service loads one only while one of its components is ticked
on the terminal's Drivers page (`[components]` in the config), and unloads it when the last one
is unticked.

Read [ARCHITECTURE.md](ARCHITECTURE.md), "The architecture", first. The drivers to copy are
`src/drivers/razer/` (a USB HID device, the smallest), `src/drivers/fusion2/` (USB HID with
settings) and `src/drivers/openrgb/` (a network protocol).

## First: do you need a new driver?

```text
Does the device appear in OpenLinkHub or OpenRGB?
 ├─ yes → no new driver: the openrgb driver already sees it.
 │        Taken for the right thing? (`wallpapercolor devices` shows the guesses)
 │         ├─ yes → done
 │         └─ no  → improve classify() in src/drivers/openrgb/protocol.cpp
 │                  (or the user adds a [[component]] rule)
 │        A new kind of thing (a headset)? → add a Component (step 2)
 └─ no  → a new driver: the steps below
```

A new driver is only for hardware we talk to **directly** (`/dev/hidraw`, USB, I²C, a vendor
daemon's socket…), as `fusion2` does for the Gigabyte chip so that OpenRGB doesn't have to run
all the time.

## The steps, with a keyboard as the example

A made-up keyboard, "Acme K1" (USB HID `1234:5678`), driven through `/dev/hidraw`.

### 1. The folder

```text
src/drivers/acmek1/
  protocol.{hpp,cpp}   pure: its [acmek1] settings, its zones, the reports byte by byte
  driver.{hpp,cpp}     the port to the keyboard, and the Driver the programs see
  keyboard.{hpp,cpp}   the real keyboard: sends the reports through /dev/hidraw
  module.cpp           what the module exports, with the real keyboard wired in
  CMakeLists.txt
tests/test_acmek1.cpp
```

Three kinds of code, kept apart so each can be tested alone:

- **`protocol`**: everything that is only data. How the settings are read and checked, which
  zones the keyboard has, and the bytes of every report. No file, no device: a test checks the
  bytes for known colours.
- **`driver`**: the logic. `Keyboard` is the driver's port to its device (open, close, send);
  `KeyboardDriver` implements `wpc::Driver` over it: when to open, what "not plugged in" means,
  what a failure does. A test runs it with a fake keyboard.
- **`keyboard`**: the real port: finds the device, opens it, sends the reports `protocol` built.
  No decisions here.

Nothing outside the folder includes these headers: the programs only ever call
`wpc_driver_info()`.

### 2. The component

If the hardware is a new kind of thing, add it at the **end** of `enum class Component` in
`src/drivers/sdk/hardware.hpp`, with its name in `ALL_COMPONENTS`, `component_name()` and
`component_label()`. That is the only change outside the driver's folder (`Keyboard` already
exists). Any change in `drivers/sdk/` changes the build id: every module must be rebuilt and
installed again, which `install.sh` does.

### 3. `CMakeLists.txt`

```cmake
# Acme K1 keyboard, through /dev/hidraw.
wpc_driver(acmek1 SOURCES protocol.cpp keyboard.cpp driver.cpp)
  # LIBRARIES PkgConfig::USB          only what this driver needs: loaded only with it
```

and `add_subdirectory(drivers/acmek1)` in `src/CMakeLists.txt`, next to the others.

`wpc_driver()` (`cmake/drivers.cmake`) builds `wpc-driver-acmek1.so` from those sources plus
`module.cpp`, links `utils/` into it privately and exports only `wpc_driver_info`. It also makes
`wpc_driver_acmek1`, a static library of the same code for the tests. `install.sh` and the
release pick up every module on their own.

### 4. The protocol

```cpp
// protocol.hpp: the Acme K1 as pure functions.
namespace wpc::acmek1 {
struct KeyboardSettings {
    bool per_key = true;
};
/// Throws wpc::Error for a wrong value.
KeyboardSettings read_settings(const DriverSettings& table);

Device keyboard_device(uint32_t keys);

using Report = std::array<uint8_t, 65>;
/// The report that shows `colours` from key `first` on (at most 20 a report).
Report colours_report(size_t first, std::span<const Rgb> colours);
}  // namespace wpc::acmek1
```

Parse, don't validate: `read_settings()` turns the table into typed values and refuses what it
can't use, so the rest of the driver never checks them again.

### 5. The driver

```cpp
// driver.hpp
namespace wpc::acmek1 {
/// What the driver needs from the keyboard.
class Keyboard {
public:
    virtual ~Keyboard() = default;
    /// Finds and opens the keyboard; false when it isn't plugged in. Throws wpc::Error when it
    /// is there but can't be used (no permission).
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    /// How many keys it has. Throws wpc::Error.
    virtual uint32_t keys() = 0;
    /// Sends one report. Throws wpc::Error.
    virtual void send(const Report& report) = 0;
};

inline constexpr Component COMPONENTS[] = {Component::Keyboard};

class KeyboardDriver final : public Driver {
public:
    KeyboardDriver(KeyboardSettings settings, std::unique_ptr<Keyboard> keyboard)
        : settings_(settings), keyboard_(std::move(keyboard)) {}
    std::vector<Device> devices() override;
    void show(size_t device, std::span<const ZoneFrame> zones) override;

private:
    KeyboardSettings settings_;
    std::unique_ptr<Keyboard> keyboard_;
};
}  // namespace wpc::acmek1

// driver.cpp
std::vector<Device> KeyboardDriver::devices() {
    if (!keyboard_->is_open() && !keyboard_->open()) return {};  // not plugged in is not an error
    return {keyboard_device(keyboard_->keys())};
}

void KeyboardDriver::show(size_t, std::span<const ZoneFrame> zones) {
    if (!keyboard_->is_open()) throw Error("Acme K1: not open");
    try {
        for (const ZoneFrame& z : zones) {
            for (size_t first = 0; first < z.colours.size(); first += 20) {
                keyboard_->send(colours_report(first, z.colours.subspan(first, std::min<size_t>(20, z.colours.size() - first))));
            }
        }
    } catch (const Error&) {
        keyboard_->close();  // opened again by the next devices()
        throw;               // the service shows it and tries again later
    }
}
```

### 6. The real keyboard

`keyboard.{hpp,cpp}`: `class HidKeyboard final : public Keyboard`. `open()` finds the device
with `utils/hidraw` and opens it, `send()` writes the report, `close()` drops the file. No
decisions: the driver makes them.

### 7. `module.cpp`

The composition root: reads the settings and wires the real keyboard into the driver.

```cpp
// What wpc-driver-acmek1.so exports: its DriverInfo, with the real keyboard wired in.
#include "drivers/acmek1/driver.hpp"
#include "drivers/acmek1/keyboard.hpp"

namespace {
std::unique_ptr<wpc::Driver> create(const wpc::DriverSettings& table) {
    return std::make_unique<wpc::acmek1::KeyboardDriver>(wpc::acmek1::read_settings(table),
                                                         std::make_unique<wpc::acmek1::HidKeyboard>());
}

const wpc::DriverInfo INFO = {
    .built_for = WPC_BUILD_ID,      // the programs refuse modules from another build
    .name = "acmek1",
    .config_table = "acmek1",       // its table in config.toml
    .components = wpc::acmek1::COMPONENTS,
    .create = create,
};
}  // namespace

extern "C" const wpc::DriverInfo* wpc_driver_info() {
    return &INFO;
}
```

Its settings come from its own table in `config.toml`:

```toml
[acmek1]
per_key = true
```

`DriverSettings` offers `boolean()`, `number()`, `string()` and `numbers()` (numbers are always
finite: the config reader refuses NaN and infinities). A key with the wrong type throws
`wpc::Error`; a key the driver never reads is logged as an unknown setting, so typos show up.

A setting that is one of a few words can also be shown on the terminal's Drivers page, under
the components, where `←` `→` changes it. List it in `DriverInfo::options` (the first choice is
the default) next to `COMPONENTS` in `driver.hpp`:

```cpp
inline constexpr const char* LAYOUTS[] = {"ansi", "iso"};  // as read_settings reads them
inline constexpr DriverOption OPTIONS[] = {
    {"layout", "Layout", LAYOUTS, "the shape of the Enter key"},
};
```

and `.options = wpc::acmek1::OPTIONS` in `INFO`. Test that every choice is one `read_settings`
accepts, as `test_razer.cpp` does.

## The rules every driver follows

- **Do nothing until you're needed.** `create()` only reads settings: it opens nothing, starts
  no threads, allocates nothing big. `devices()` opens the hardware.
- **Leave nothing running when you're destroyed.** The module may be unloaded right after the
  destructor; a thread or callback still alive would run on unmapped code. Simplest: no threads
  at all. The programs call the driver on their own thread.
- **Nothing that holds your code leaves the module.** Return plain values (devices, zones,
  colours). Exceptions are caught by the host as they leave and copied (see ARCHITECTURE.md,
  "Plugins behind a narrow, guarded boundary"), so throw `wpc::Error` freely; but never hand the
  programs a `std::function`, an object with virtual functions, or anything else whose code is
  yours.
- **No global state.** Everything lives in the driver's objects, because the module can be
  loaded and unloaded many times. Constants (`constexpr`, `INFO`) are fine; `static` variables
  in functions and singletons are not (they also make GCC emit "unique" symbols, and glibc
  never unloads such a module). The test "the real driver modules load and unload" and
  `packaging/release.sh` catch them.
- **Absent hardware is not an error.** No devices. Throw `wpc::Error` only for "it's here but I
  can't use it", with a message the user can act on: it shows on the Drivers page and in
  `wallpapercolor status`.
- **Keep the connection, drop it on error.** The driver lives as long as its components are
  ticked; it reopens on the next `devices()`.
- **Trust nothing the device or server says.** Check every length before reading it (as
  `openrgb::Reader` does): a short or hostile answer is an error, never a read past a buffer.
- **Bound every wait.** `show()` is called 25 times a second during a fade, on the thread that
  runs the whole service: write and return, no sleeping, no reading back, and a timeout on
  every blocking call.
- **Only touch the zones you're given.** `show()` gets only the ticked components' zones.
- **Say what each zone is.** The right `Component`, and `Shape`: `Ring` for fans and pumps,
  `Line` for RAM, strips and keyboards, `Spot` for single-colour lights.
- **Use the colours you're given.** They come calibrated.
- **Never save to the device's flash.** After a reboot the hardware shows its own default.
- **Include only your own folder, `drivers/sdk/` and `utils/`.** If two drivers need the same
  plain code, it goes to `utils/`. `ctest` checks it.
- **Build messages with `cat()`** (`utils/text.hpp`), not `std::format`, which adds about 70 KB
  to every module that uses it.

## Access to the device

For a `/dev/hidraw` or USB device, a line in `udev/70-wallpapercolor.rules` lets the logged-in
user open it:

```text
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1234", ATTRS{idProduct}=="5678", TAG+="uaccess"
```

## Tests

Add `test_acmek1.cpp` to `tests/CMakeLists.txt` and link `wpc_driver_acmek1` into `wpc_tests`,
as `test_fusion2.cpp` and `test_razer.cpp` are. Test:

- the **protocol**: the settings (wrong values are errors, missing keys give the defaults), the
  zones, and the reports byte by byte;
- the **driver** with a fake of its port (`FakeBoard` in `test_fusion2.cpp`): no device gives
  no devices and no error; a failure closes the connection; unplugged and plugged in again, it
  opens again.

Add `"acmek1"` to the test "the real driver modules load and unload"
(`tests/test_driver_host.cpp`): it checks that the module loads, matches the build and really
unloads. `ctest` also checks the new folder's includes.

## Docs

- README: the hardware in "Requirements", its `[acmek1]` table in "Configuration".
- ARCHITECTURE.md: the driver in "The modules".

## Checking it

```sh
./install.sh                                      # builds, tests, installs every module
wallpapercolor drivers                            # Keyboard  acmek1
wallpapercolor devices                            # acmek1 (keyboard) / Acme K1 / Keys … keyboard
wallpapercolor set ff0000                         # it turns red

wallpapercolor drivers off keyboard
grep acmek1 /proc/$(pidof wallpapercolord)/maps   # nothing: unloaded
```
