// The settings: everything config.toml can say, in parts (LED mode and fade, colour picking,
// components, calibrations, the drivers' tables). Plain values; reading and writing the file is
// the settings_file service (shared/infrastructure/services).

#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "drivers/sdk/driver_settings.hpp"
#include "drivers/sdk/hardware.hpp"
#include "shared/domain/colours/calibration.hpp"
#include "shared/domain/colours/extract.hpp"

namespace wpc {

inline constexpr std::array<bool, ALL_COMPONENTS.size()> make_all_on() {
    std::array<bool, ALL_COMPONENTS.size()> on{};
    on.fill(true);
    return on;
}

/// The `[led]` table.
struct LedConfig {
    Mode mode = Mode::Arc;
    /// Seconds a colour change takes (0 = at once). At most 1.5.
    double fade = 0.6;
    bool operator==(const LedConfig&) const = default;
};

/// One `[[component]]` entry: says what a zone is when the driver guessed wrong. Matched like
/// a calibration; the first matching entry wins.
struct ComponentRule {
    std::string device;
    /// Zone name; nullopt = every zone of the device.
    std::optional<std::string> zone;
    Component is = Component::Other;
    bool operator==(const ComponentRule&) const = default;
};

/// `[components]` and `[[component]]`: what is painted, and corrections of what a zone is.
struct ComponentSettings {
    /// What is painted, by Component. All on unless the file says otherwise.
    std::array<bool, ALL_COMPONENTS.size()> on = make_all_on();
    /// Whether the file has a `[components]` table (the terminal's Drivers page asks first).
    bool chosen = false;
    std::vector<ComponentRule> rules;

    bool wants(Component c) const { return on[static_cast<size_t>(c)]; }
    /// What `zone` of `device` is: the first matching rule, else `guess`.
    Component component_of(std::string_view device, std::string_view zone, Component guess) const;

    bool operator==(const ComponentSettings&) const = default;
};

/// The drivers' own tables ([openrgb], [motherboard]…), by name, as written. Each driver reads
/// its own (drivers/sdk/driver_settings.hpp).
struct DriverTables {
    std::map<std::string, DriverSettings> tables;

    /// The table `[name]`; an empty one if the file has none.
    DriverSettings of(const std::string& name) const;

    bool operator==(const DriverTables&) const = default;
};

/// Everything in the config file, in parts: each piece of code takes only the part it uses.
struct Config {
    LedConfig led;
    ExtractConfig extract;
    ComponentSettings components;
    /// Colour correction for LEDs that render colours off (e.g. weak blue: white looks yellow).
    Calibrations calibration;
    DriverTables drivers;

    bool operator==(const Config&) const = default;
};


}  // namespace wpc
