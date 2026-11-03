// Model: what the fusion2 driver keeps between calls.

#pragma once

#include <array>
#include <vector>

#include "drivers/fusion2/domain/zones.hpp"

namespace wpc::fusion2 {

struct BoardState {
    BoardSettings settings;
    /// The device, and where each zone's LEDs go.
    BoardLayout layout;
    /// What each header shows.
    std::array<std::vector<Rgb>, HEADERS> headers;
};

}  // namespace wpc::fusion2
