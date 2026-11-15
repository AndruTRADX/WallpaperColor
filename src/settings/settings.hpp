// The settings: everything config.toml can say, in parts (LED mode and fade, colour picking,
// components, calibrations, the drivers' tables), with the range of every number. Plain values
// that are always valid: settings_file.hpp is the only way in from text, and it checks them.

#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "colours/extract.hpp"
#include "colours/paint.hpp"
#include "drivers/sdk/driver_settings.hpp"
#include "drivers/sdk/hardware.hpp"

namespace wpc {

/// The numbers a setting may be: from `min` to `max`, both included.
struct Range {
    double min;
    double max;
    bool contains(double v) const { return v >= min && v <= max; }  // false for NaN
};

/// The range of every number setting. Outside it, the file has a mistake (said with its line),
/// except a longer fade, which is taken as the longest.
namespace ranges {
inline constexpr Range FADE{0.0, 1.5};  // seconds
inline constexpr Range MIN_SATURATION{0.0, 1.0};
inline constexpr Range PASTEL_GAIN{0.0, 100.0};
inline constexpr Range SKIN_WEIGHT{0.0, 1.0};
inline constexpr Range MIN_CHROMA{0.0, 1.0};
inline constexpr Range MIN_SHARE{0.0, 1.0};
inline constexpr Range PALETTE_SIZE{1, 64};    // colours: each is a cut of the picture
inline constexpr Range SAMPLE_SIZE{1, 1024};   // pixels per side: memory and time grow with its square
inline constexpr Range GAIN{0.0, 1.0};         // calibration; outside, the nearest end is used
}  // namespace ranges

inline constexpr std::array<bool, ALL_COMPONENTS.size()> make_all_on() {
    std::array<bool, ALL_COMPONENTS.size()> on{};
    on.fill(true);
    return on;
}

/// The `[led]` table.
struct LedConfig {
    Mode mode = Mode::Arc;
    /// Seconds a colour change takes (0 = at once), within ranges::FADE.
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
