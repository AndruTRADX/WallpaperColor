// Feature: what the Drivers page shows next to each component: what every installed driver
// finds that is that component. Uses list_devices: every driver is loaded briefly.

#pragma once

#include <array>
#include <string>
#include <vector>

#include "shared/domain/settings/settings.hpp"
#include "terminal/application/contracts/external/driver_modules.hpp"

namespace wpc {

struct Detection {
    std::vector<DriverModule> modules;
    /// For each component: what was found, like "iCUE COMMANDER Core: 4 zones".
    std::array<std::vector<std::string>, ALL_COMPONENTS.size()> found;
    /// Drivers that could not look: "openrgb: cannot reach the OpenRGB server…".
    std::vector<std::string> problems;
};

Detection detect_hardware(DriverModules& drivers, const Config& config);

}  // namespace wpc
