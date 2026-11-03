#include "drivers/fusion2/domain/zones.hpp"

#include <algorithm>

namespace wpc::fusion2 {

namespace {

/// Names as the user sees them (and matches them in [[calibration]]). Every fan is called the
/// same, so split mode gives all of them the same colour.
constexpr const char* DEVICE_NAME = "Motherboard";
constexpr const char* ONBOARD_ZONE = "Onboard";
constexpr const char* HEADER_ZONE = "ARGB header";
constexpr const char* FAN_ZONE = "ARGB fan";

}  // namespace

BoardLayout board_layout(const BoardSettings& settings) {
    BoardLayout layout;
    layout.device = Device{"motherboard", DEVICE_NAME, {}, {}};
    layout.device.zones.push_back(Zone{ONBOARD_ZONE, 1, Component::Motherboard, Shape::Spot});
    layout.places.push_back({-1, 0});
    for (int h = 0; h < HEADERS; ++h) {
        const std::vector<uint32_t>& fans = settings.argb_headers[h];
        if (fans.empty()) {
            // Unknown: one colour, which the board puts on every LED the header drives.
            layout.device.zones.push_back(Zone{HEADER_ZONE, 1, Component::Argb, Shape::Ring});
            layout.places.push_back({h, 0});
            layout.header_leds[h] = 1;
            continue;
        }
        size_t first = 0;
        for (uint32_t leds : fans) {
            layout.device.zones.push_back(Zone{FAN_ZONE, leds, Component::Argb, Shape::Ring});
            layout.places.push_back({h, first});
            first += leds;
        }
        layout.header_leds[h] = first;
    }
    return layout;
}

BoardFrame place_frame(const BoardLayout& layout, std::span<const ZoneFrame> zones,
                       std::array<std::vector<Rgb>, HEADERS>& headers) {
    BoardFrame frame;
    for (int h = 0; h < HEADERS; ++h) headers[h].resize(layout.header_leds[h]);
    for (const ZoneFrame& z : zones) {
        const BoardLayout::Place& p = layout.places.at(z.zone);
        if (p.header < 0) {
            if (!z.colours.empty()) frame.onboard = z.colours[0];
            continue;
        }
        // A header is sent whole: zones not in this frame keep what they showed.
        std::vector<Rgb>& header = headers[p.header];
        size_t n = std::min(z.colours.size(), header.size() - p.first);
        std::copy_n(z.colours.begin(), n, header.begin() + static_cast<std::ptrdiff_t>(p.first));
        frame.header_changed[p.header] = true;
    }
    return frame;
}

}  // namespace wpc::fusion2
