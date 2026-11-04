// The driver SDK: what a driver module is written against. Every module implements Driver and
// exports wpc_driver_info(); the programs only ever see this interface.
//
// A driver is built into its own file, wpc-driver-<name>.so, and the programs load it with
// drivers/host only while one of its components is chosen. How to write one:
// docs/ADDING-A-MODULE.md.
//
// Header-only, so a module needs nothing from the programs to use it.

#pragma once

#include <memory>
#include <span>
#include <vector>

#include "drivers/sdk/driver_settings.hpp"
#include "drivers/sdk/hardware.hpp"

namespace wpc {

/// One zone's colours in a frame.
struct ZoneFrame {
    size_t zone;  // index in Device::zones
    std::span<const Rgb> colours;
};

class Driver {
public:
    virtual ~Driver() = default;

    /// The devices it can paint now. Hardware that isn't there is no device, not an error.
    /// Throws wpc::Error when it is there but cannot be used (permissions, a server that
    /// doesn't answer), with a message for the user.
    virtual std::vector<Device> devices() = 0;

    /// Shows one frame on device number `device` of the last devices() call. Only the zones of
    /// chosen components are in `zones`: leave the others as they are. Called 25 times a
    /// second during a fade, so it writes and returns. Throws wpc::Error.
    virtual void show(size_t device, std::span<const ZoneFrame> zones) = 0;
};

/// A setting of the driver's config table that the terminal's Drivers page lets the user choose:
/// one of a few words, written as a string. The first choice is what the driver uses when the
/// table doesn't say.
struct DriverOption {
    const char* key;    // "direction": its key in the driver's table
    const char* label;  // "Direction": its name on the Drivers page
    std::span<const char* const> choices;
    const char* hint;   // what it changes, in a few words
};

/// What a module tells the programs about its driver.
struct DriverInfo {
    /// WPC_BUILD_ID of the build the module comes from. The programs refuse any other: a
    /// module and the programs must come from the same build (see drivers/host).
    const char* built_for;
    /// "fusion2": logs and the Drivers page.
    const char* name;
    /// Its table in the config file, without brackets: "motherboard".
    const char* config_table;
    /// What it can paint.
    std::span<const Component> components;
    /// Creates the driver. Reads its settings only: opens nothing, starts nothing.
    std::unique_ptr<Driver> (*create)(const DriverSettings& settings);
    /// The settings the Drivers page offers; none if it offers none.
    std::span<const DriverOption> options = {};
};

}  // namespace wpc

/// The one function a module exports, found with dlsym. Modules are built with hidden
/// symbols (see cmake/drivers.cmake), so this is marked to stay visible.
extern "C" [[gnu::visibility("default")]] const wpc::DriverInfo* wpc_driver_info();
