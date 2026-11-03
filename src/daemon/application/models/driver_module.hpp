// Model: an installed driver, before it is loaded.

#pragma once

#include <string>
#include <vector>

#include "drivers/sdk/hardware.hpp"

namespace wpc::inline daemon {

struct DriverModule {
    std::string name;          // "fusion2"
    std::string config_table;  // "motherboard": its table in config.toml
    std::vector<Component> components;
};

}  // namespace wpc::daemon
