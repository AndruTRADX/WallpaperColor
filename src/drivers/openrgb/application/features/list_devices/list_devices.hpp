// Feature: every device of the OpenRGB server, with its zones classified. Connects first if
// there is no connection; a failure drops it, so the next call connects again.

#pragma once

#include <vector>

#include "drivers/openrgb/application/contracts/openrgb_server.hpp"
#include "drivers/openrgb/application/models/server_state.hpp"

namespace wpc::openrgb {

/// The devices, in the order of state.controllers. Throws wpc::Error naming the server.
std::vector<Device> list_devices(OpenRgbServer& server, ServerState& state);

}  // namespace wpc::openrgb
