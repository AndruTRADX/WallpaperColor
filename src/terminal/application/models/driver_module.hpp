// Model: an installed driver, before it is loaded.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "drivers/sdk/hardware.hpp"

namespace wpc::inline terminal {

struct DriverModule {
    std::string name;          // "fusion2"
    std::string config_table;  // "motherboard": its table in config.toml
    std::vector<Component> components;

    bool paints(Component c) const { return std::find(components.begin(), components.end(), c) != components.end(); }
};

}  // namespace wpc::terminal
