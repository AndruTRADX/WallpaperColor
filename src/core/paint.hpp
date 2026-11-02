// Decides the colour of every LED from the picture's colours and the settings.
//
// The service, `wallpapercolor set` and the terminal UI's preview all paint through this, so
// the preview always shows what the service sends.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/color.hpp"
#include "core/config.hpp"

namespace wpc {

class Painter {
public:
    /// `colours`: one to three colours, the main colour first (Extraction::stops gives them).
    Painter(std::vector<Rgb> colours, Mode mode, const Config& config);

    /// The colours of the `leds` LEDs of one zone, in LED order.
    /// - arc: the colours as a gradient; around the zone when `ring` (fans, pumps), else from
    ///   one end to the other (RAM, top to bottom);
    /// - single: the main colour everywhere;
    /// - split: one colour for the whole zone. Each new zone name takes the next colour, so
    ///   zones of the same kind match (both RAM sticks, all four fans). Call it for the zones in
    ///   device order, so the same zone always gets the same colour.
    /// Then each channel is scaled by the zone's calibration gain.
    std::vector<Rgb> zone(std::string_view device, std::string_view zone, uint32_t leds, bool ring);

private:
    std::vector<Rgb> colours_;
    Mode mode_;
    const Config& config_;
    /// Zone names in the order split mode met them; the position picks the colour.
    std::vector<std::string> zone_names_;
};

/// `c` with each channel multiplied by its gain (0-1).
Rgb scale(Rgb c, std::array<float, 3> gain);

}  // namespace wpc
