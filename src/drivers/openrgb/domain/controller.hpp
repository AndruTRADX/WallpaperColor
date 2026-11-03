// An OpenRGB controller as the server describes it: a device with zones and LEDs.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "drivers/sdk/hardware.hpp"

namespace wpc::openrgb {

/// OpenRGB device types (the ones told apart).
namespace device_type {
inline constexpr int32_t MOTHERBOARD = 0;
inline constexpr int32_t DRAM = 1;
inline constexpr int32_t GPU = 2;
inline constexpr int32_t COOLER = 3;
inline constexpr int32_t LED_STRIP = 4;
inline constexpr int32_t KEYBOARD = 5;
}  // namespace device_type

struct Zone {
    std::string name;
    uint32_t leds = 0;
};

struct Controller {
    /// Index used to address the controller in this session (it can change when devices change).
    uint32_t index = 0;
    int32_t kind = 0;
    std::string name;
    std::vector<Zone> zones;
    /// Current colour of every LED, in LED order.
    std::vector<Rgb> colors;

    size_t led_count() const { return colors.size(); }
};

}  // namespace wpc::openrgb
