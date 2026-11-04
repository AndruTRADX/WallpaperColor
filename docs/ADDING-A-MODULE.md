# Adding a driver module

A **module** is one **driver** built as its own file, `wpc-driver-<name>.so`. A driver speaks
one protocol and paints some **components** (fans, pump, RAM, motherboard, ARGB, keyboard…).
Every module is installed, but the service loads one only while one of its components is ticked
on the terminal's Drivers page (`[components]` in the config), and unloads it when the last one
is unticked.

A driver is built from the same layers as the programs, with the same rules: read
[ARCHITECTURE.md](ARCHITECTURE.md), "The layers", first. The two drivers to copy are
`src/drivers/fusion2/` (a USB HID device) and `src/drivers/openrgb/` (a network protocol).

## First: do you need a new driver?

```text
Does the device appear in OpenLinkHub or OpenRGB?
 ├─ yes → no new driver: the openrgb driver already sees it.
 │        Taken for the right thing? (`wallpapercolor devices` shows the guesses)
 │         ├─ yes → done
 │         └─ no  → improve classify() in src/drivers/openrgb/domain/zones.cpp
 │                  (or the user adds a [[component]] rule)
 │        A new kind of thing (a headset)? → add a Component (step 2)
 └─ no  → a new driver: the steps below
```

A new driver is only for hardware we talk to **directly** (`/dev/hidraw`, USB, I²C, a vendor
daemon's socket…), as `fusion2` does for the Gigabyte chip so that OpenRGB doesn't have to run
all the time.

## The steps, with a keyboard as the example

A made-up keyboard, "Acme K1" (USB HID `1234:5678`), driven through `/dev/hidraw`.

### 1. The folder, layer by layer

```text
src/drivers/acmek1/
  domain/settings.{hpp,cpp}                   its [acmek1] table, read with its rules
  domain/zones.{hpp,cpp}                      which zones it has, and what each one is
  application/contracts/keyboard.hpp          the interface to the protocol's service
  application/models/keyboard_state.hpp       what the driver keeps between calls
  application/features/list_devices/          the devices: opens the keyboard if needed
  application/features/show_frame/            one frame: closes the keyboard on a failure
  infrastructure/services/keyboard.{hpp,cpp}  the protocol: one concrete job per method
  presentation/driver.hpp                     the wpc::Driver the programs see: runs the features
  module.cpp                                  the composition root: what the module exports
  CMakeLists.txt
tests/test_acmek1.cpp
```

What goes where follows the programs' rules:

- **domain**: the rules of its entities, pure. Which zones the keyboard has, how its settings
  are read and checked.
- **contract**: only `virtual … = 0` methods, one job each. Opening and closing are methods too.
- **service**: talks to the hardware, one concrete job per method, never another service. It
  keeps the connection; it never decides when to open, retry or close.
- **features**: the logic. When to open, what "not plugged in" means, what a failure does.
  Whenever two service methods are needed (`is_open()` then `open()`, `keys()` then `set()`),
  the feature calls both.
- **model**: what the driver keeps between calls (its settings, what it found).
- **presentation**: the `Driver`. It holds the service and the model, and only runs the
  features: no `if`, no `try`, no utils.

Nothing outside the folder includes these headers. The programs only ever call
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
wpc_driver(acmek1 SOURCES
  domain/settings.cpp domain/zones.cpp
  application/features/list_devices/list_devices.cpp application/features/show_frame/show_frame.cpp
  infrastructure/services/keyboard.cpp)
  # LIBRARIES PkgConfig::USB          only what this driver needs: loaded only with it
```

and `add_subdirectory(drivers/acmek1)` in `src/CMakeLists.txt`, next to the others.

`wpc_driver()` (`cmake/drivers.cmake`) builds `wpc-driver-acmek1.so` from those sources plus
`module.cpp`, links `utils/` into it privately and exports only `wpc_driver_info`. It also makes
`wpc_driver_acmek1`, a static library of the same code for the tests. `install.sh` and the
release pick up every module on their own.

### 4. The contract and the model

```cpp
// application/contracts/keyboard.hpp
// What the acmek1 driver needs from the keyboard.
#pragma once
#include <span>
#include "drivers/sdk/hardware.hpp"

namespace wpc::acmek1 {
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
    /// Shows `colours`, one per key (one colour: every key). Throws wpc::Error.
    virtual void set(std::span<const Rgb> colours) = 0;
};
}  // namespace wpc::acmek1
```

```cpp
// application/models/keyboard_state.hpp
// Model: what the acmek1 driver keeps between calls.
#pragma once
#include "drivers/acmek1/domain/settings.hpp"

namespace wpc::acmek1 {
struct KeyboardState {
    KeyboardSettings settings;
};
}  // namespace wpc::acmek1
```

### 5. The features

```cpp
// application/features/list_devices/list_devices.cpp
std::vector<Device> list_devices(Keyboard& keyboard, const KeyboardState& state) {
    if (!keyboard.is_open() && !keyboard.open()) return {};  // not plugged in is not an error
    return {keyboard_device(state.settings, keyboard.keys())};  // domain/zones
}

// application/features/show_frame/show_frame.cpp
void show_frame(Keyboard& keyboard, std::span<const ZoneFrame> zones) {
    if (!keyboard.is_open()) throw Error("Acme K1: not open");
    try {
        for (const ZoneFrame& z : zones) keyboard.set(z.colours);
    } catch (const Error&) {
        keyboard.close();  // opened again by the next list_devices()
        throw;             // the service shows it and tries again later
    }
}
```

### 6. The service

`infrastructure/services/keyboard.{hpp,cpp}`: `class HidKeyboard final : public Keyboard`.
`open()` finds the device with `utils/hidraw` and opens it, `set()` builds and sends the
packets, `close()` drops the file. Each method one job, no decisions, no other service. The
byte order on the wire, if the device wants one, is the service's job.

### 7. The presentation

```cpp
// presentation/driver.hpp
// The acmek1 driver as the programs see it: the module's Driver. It only runs the features.
#pragma once
#include <memory>
#include "drivers/acmek1/application/features/list_devices/list_devices.hpp"
#include "drivers/acmek1/application/features/show_frame/show_frame.hpp"
#include "drivers/sdk/driver.hpp"

namespace wpc::acmek1 {
inline constexpr Component COMPONENTS[] = {Component::Keyboard};

class KeyboardDriver final : public Driver {
public:
    KeyboardDriver(KeyboardSettings settings, std::unique_ptr<Keyboard> keyboard)
        : keyboard_(std::move(keyboard)), state_{std::move(settings)} {}
    std::vector<Device> devices() override { return list_devices(*keyboard_, state_); }
    void show(size_t, std::span<const ZoneFrame> zones) override { show_frame(*keyboard_, zones); }

private:
    std::unique_ptr<Keyboard> keyboard_;
    KeyboardState state_;
};
}  // namespace wpc::acmek1
```

### 8. `module.cpp`

The composition root: reads the settings (domain) and wires the real service into the driver.

```cpp
// What wpc-driver-acmek1.so exports: its DriverInfo, with the real keyboard service wired in.
#include "drivers/acmek1/infrastructure/services/keyboard.hpp"
#include "drivers/acmek1/presentation/driver.hpp"

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

`DriverSettings` offers `boolean()`, `number()`, `string()` and `numbers()`. A key with the
wrong type throws `wpc::Error`; a key the driver never reads is logged as an unknown setting,
so typos show up.

A setting that is one of a few words can also be shown on the terminal's Drivers page, under
the components, where `←` `→` changes it. List it in `DriverInfo::options` (the first choice is
the default) next to `COMPONENTS` in `presentation/driver.hpp`:

```cpp
inline constexpr const char* LAYOUTS[] = {"ansi", "iso"};  // as domain/settings reads them
inline constexpr DriverOption OPTIONS[] = {
    {"layout", "Layout", LAYOUTS, "the shape of the Enter key"},
};
```

and `.options = wpc::acmek1::OPTIONS` in `INFO`. Test that every choice is one `read_settings`
accepts, as `test_razer.cpp` does.

## The rules every driver follows

Besides the layers:

- **Do nothing until you're needed.** `create()` only reads settings: it opens nothing, starts
  no threads, allocates nothing big. The `list_devices` feature opens the hardware.
- **Leave nothing running when you're destroyed.** The module may be unloaded right after the
  destructor; a thread or callback still alive would run on unmapped code. Simplest: no threads
  at all. The programs call the driver on their own thread.
- **No global state.** Everything lives in the driver's objects, because the module can be
  loaded and unloaded many times. Constants (`constexpr`, `INFO`) are fine; `static` variables
  in functions and singletons are not (they also make GCC emit "unique" symbols, and glibc
  never unloads such a module). The test "the real driver modules load and unload" and
  `packaging/release.sh` catch them.
- **Absent hardware is not an error.** No devices. Throw `wpc::Error` only for "it's here but I
  can't use it", with a message the user can act on: it shows on the Drivers page and in
  `wallpapercolor status`.
- **Keep the connection, drop it on error.** The driver lives as long as its components are
  ticked; the features reopen on the next `devices()`.
- **Only touch the zones you're given.** `show()` gets only the ticked components' zones.
- **Say what each zone is.** The right `Component`, and `Shape`: `Ring` for fans and pumps,
  `Line` for RAM, strips and keyboards, `Spot` for single-colour lights.
- **Use the colours you're given.** They come calibrated.
- **Never save to the device's flash.** After a reboot the hardware shows its own default.
- **Include only your own folder, `drivers/sdk/` and `utils/`**, each layer only what it may.
  If two drivers need the same plain code, it goes to `utils/`.
- **Build messages with `cat()`** (`utils/text.hpp`), not `std::format`, which adds about 70 KB
  to every module that uses it.
- **Show it fast.** `show()` is called 25 times a second during a fade: write and return, no
  sleeping, no reading back.

## Access to the device

For a `/dev/hidraw` or USB device, a line in `udev/70-wallpapercolor.rules` lets the logged-in
user open it:

```text
KERNEL=="hidraw*", ATTRS{idVendor}=="1234", ATTRS{idProduct}=="5678", TAG+="uaccess"
```

## Tests

Add `test_acmek1.cpp` to `tests/CMakeLists.txt` and link `wpc_driver_acmek1` into `wpc_tests`,
as `test_fusion2.cpp` and `test_openrgb.cpp` are. Test:

- the **features** with a fake of the contract (`FakeBoard` in `test_fusion2.cpp`): no device
  gives no devices and no error; a failure closes the connection;
- the **service** on bytes: the packets for a known colour list, byte for byte;
- the **domain**: the settings (wrong types are errors, missing keys give the defaults) and the
  zones.

Add `"acmek1"` to the test "the real driver modules load and unload" (`tests/test_modules.cpp`):
it checks that the module loads, matches the build and really unloads. `ctest` also runs the
layer checks on the new folder.

## Docs

- README: the hardware in "Requirements", its `[acmek1]` table in "Configuration".
- ARCHITECTURE.md: the driver in "Where things go".

## Checking it

```sh
./install.sh                                      # builds, tests, installs every module
wallpapercolor drivers                            # Keyboard  acmek1
wallpapercolor devices                            # acmek1 (keyboard) / Acme K1 / Keys … keyboard
wallpapercolor set ff0000                         # it turns red

wallpapercolor drivers off keyboard
grep acmek1 /proc/$(pidof wallpapercolord)/maps   # nothing: unloaded
```
