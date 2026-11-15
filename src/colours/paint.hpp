// Decides the colour of every LED from the picture's colours and the settings, with the
// colour correction (calibration) of each device.
//
// The service, `wallpapercolor set` and the terminal UI's preview all paint through this, so
// the preview always shows what the service sends.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "colours/extract.hpp"
#include "drivers/sdk/hardware.hpp"

namespace wpc {

/// Colour correction for LEDs that show colours off (a weak blue makes white look yellow): one
/// gain per channel, for a device or one zone of it. An `[[calibration]]` entry of the config.
/// Names match case-insensitively and partially ("fan" matches "8-LED Series Fan").
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

class Painter {
public:
    /// `colours`: one to three colours, the main colour first (Extraction::stops gives them).
    Painter(std::vector<Rgb> colours, Mode mode, Calibrations calibrations);

    /// The colours of the `leds` LEDs of one zone, in LED order.
    /// - arc: the colours as a gradient; around the zone for a Ring (fans, pumps), else from
    ///   one end to the other (RAM, top to bottom);
    /// - single: the main colour everywhere;
    /// - split: one colour for the whole zone. Each new zone name takes the next colour, so
    ///   zones of the same kind match (both RAM sticks, all four fans). Call it for the zones in
    ///   device order, so the same zone always gets the same colour.
    /// Then each channel is scaled by the zone's calibration gain.
    std::vector<Rgb> zone(std::string_view device, std::string_view zone, uint32_t leds, Shape shape);

private:
    std::vector<Rgb> colours_;
    Mode mode_;
    Calibrations calibrations_;  // a copy: a Painter never points into settings that may change
    /// Zone names in the order split mode met them; the position picks the colour.
    std::vector<std::string> zone_names_;
};

/// `c` with each channel multiplied by its gain (0-1).
Rgb scale(Rgb c, std::array<float, 3> gain);

}  // namespace wpc
