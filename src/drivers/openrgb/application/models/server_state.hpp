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
    /// How many zones each of them has as a device (classify()), so a frame needn't ask again.
    std::vector<size_t> zone_counts;
};

}  // namespace wpc::openrgb
