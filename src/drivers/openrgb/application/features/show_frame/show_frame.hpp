// Feature: one frame on device `device`: the whole controller in one message when every zone is
// in it, else zone by zone. A failure drops the connection: the next list_devices() connects
// again.

#pragma once

#include <span>

#include "drivers/openrgb/application/contracts/openrgb_server.hpp"
#include "drivers/openrgb/application/models/server_state.hpp"
#include "drivers/sdk/driver.hpp"

namespace wpc::openrgb {

/// Throws wpc::Error.
void show_frame(OpenRgbServer& server, const ServerState& state, size_t device, std::span<const ZoneFrame> zones);

}  // namespace wpc::openrgb
