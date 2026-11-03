// Model: what the openrgb driver keeps between calls.

#pragma once

#include <vector>

#include "drivers/openrgb/domain/controller.hpp"
#include "drivers/openrgb/domain/settings.hpp"

namespace wpc::openrgb {

struct ServerState {
    ClientSettings settings;
    /// From the last list_devices(): what show_frame()'s device numbers mean.
    std::vector<Controller> controllers;
};

}  // namespace wpc::openrgb
