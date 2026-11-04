// Model: an installed driver, before it is loaded.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "drivers/sdk/hardware.hpp"

namespace wpc::inline terminal {

/// A setting of a driver's table that the Drivers page lets the user choose: one of a few
/// words. The first choice is the driver's default.
struct ModuleOption {
    std::string key;    // "direction"
    std::string label;  // "Direction"
    std::vector<std::string> choices;
    std::string hint;
};

struct DriverModule {
    std::string name;          // "fusion2"
    std::string config_table;  // "motherboard": its table in config.toml
    std::vector<Component> components;
    /// The settings the Drivers page offers for it.
    std::vector<ModuleOption> options = {};

    bool paints(Component c) const { return std::find(components.begin(), components.end(), c) != components.end(); }
};

}  // namespace wpc::terminal
