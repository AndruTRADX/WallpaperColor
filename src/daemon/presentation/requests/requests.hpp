// Presentation: each request from the terminal runs its feature and gets its answer.

#pragma once

#include "daemon/application/models/service_state.hpp"
#include "shared/models/protocol/messages.hpp"

namespace wpc {

/// Runs the feature of `request`. `paint_now` is set when the wallpaper must be painted again
/// at once (the request changed what is shown).
ipc::Response run_request(ipc::Request request, ServiceState& state, bool& paint_now);

}  // namespace wpc
