// The `[motherboard]` table of the config file, and its rules.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "drivers/sdk/driver_settings.hpp"

namespace wpc::fusion2 {

/// ARGB headers on the board.
inline constexpr int HEADERS = 2;

struct BoardSettings {
    /// The colour order of what is plugged into the ARGB headers ("GRB" for most fans).
    std::string argb_order = "GRB";
    /// What is chained on ARGB header 1 and 2: the LEDs of each fan or strip, in order
    /// ([8, 8] = two fans of 8 LEDs). Empty = unknown: the whole header gets one colour.
    std::array<std::vector<uint32_t>, HEADERS> argb_headers;
};

/// Throws wpc::Error for a wrong value.
BoardSettings read_settings(const DriverSettings& table);

}  // namespace wpc::fusion2
