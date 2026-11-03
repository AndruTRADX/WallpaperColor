// Colour correction for LEDs that show colours off (a weak blue makes white look yellow): one
// gain per channel, for a device or one zone of it. The `[[calibration]]` entries of the config.

#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wpc {

/// One entry. Names match case-insensitively and partially ("fan" matches "8-LED Series Fan").
struct Calibration {
    /// Device name as `wallpapercolor devices` shows it.
    std::string device;
    /// Zone name; nullopt = every zone of the device.
    std::optional<std::string> zone;
    /// Red, green and blue gains, 0-1 (lower the channels that are too strong).
    std::array<double, 3> gain = {1.0, 1.0, 1.0};
    bool operator==(const Calibration&) const = default;
};

struct Calibrations {
    std::vector<Calibration> entries;

    /// Gains for `zone` of `device`: the first matching entry, else 1, 1, 1.
    std::array<float, 3> gain(std::string_view device, std::string_view zone) const;

    bool operator==(const Calibrations&) const = default;
};

}  // namespace wpc
