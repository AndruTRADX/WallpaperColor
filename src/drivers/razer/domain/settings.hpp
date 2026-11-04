// The `[keyboard]` table of the config file, and its rules.

#pragma once

#include <cstdint>

#include "drivers/sdk/driver_settings.hpp"

namespace wpc::razer {

/// Which way the colours run over the keys.
enum class Direction : uint8_t {
    Across,  // left to right
    Down,    // top row to bottom row
};

struct KeyboardSettings {
    Direction direction = Direction::Across;
};

/// Throws wpc::Error for a wrong value.
KeyboardSettings read_settings(const DriverSettings& table);

}  // namespace wpc::razer
