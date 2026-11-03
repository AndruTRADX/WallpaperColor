// Feature: what `wallpapercolor status` and the terminal UI show.

#pragma once

#include "daemon/application/models/service_state.hpp"
#include "shared/models/protocol/messages.hpp"

namespace wpc {

ipc::Status report_status(const ServiceState& state);

}  // namespace wpc
