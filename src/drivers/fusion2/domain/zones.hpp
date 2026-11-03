// The board as a device: its onboard lights as one zone (component `motherboard`), then what is
// plugged into the two ARGB headers (component `argb`), one zone per fan when the settings say
// what is there, else one zone per header. And where each zone's colours go.

#pragma once

#include <array>
#include <optional>
#include <span>
#include <vector>

#include "drivers/fusion2/domain/settings.hpp"
#include "drivers/sdk/driver.hpp"

namespace wpc::fusion2 {

/// The device, and where each of its zones' LEDs go.
struct BoardLayout {
    /// Where a zone's LEDs go: header 0 or 1 from LED `first`, or the onboard lights (-1).
    struct Place {
        int header;
        size_t first;
    };
    Device device;
    std::vector<Place> places;  // one per zone of `device`
    /// LEDs each header holds (1 when its fans are unknown).
    std::array<size_t, HEADERS> header_leds{};
};

BoardLayout board_layout(const BoardSettings& settings);

/// What a frame changes: the onboard colour if its zone is in it, and each header whose zones
/// are in it (`headers` keeps every header's colours between frames).
struct BoardFrame {
    std::optional<Rgb> onboard;
    std::array<bool, HEADERS> header_changed{};
};

BoardFrame place_frame(const BoardLayout& layout, std::span<const ZoneFrame> zones,
                       std::array<std::vector<Rgb>, HEADERS>& headers);

}  // namespace wpc::fusion2
