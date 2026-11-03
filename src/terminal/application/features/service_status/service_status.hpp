// Feature: what the service is doing (`wallpapercolor status`, the terminal UI's header).

#pragma once

#include <vector>

#include "shared/models/protocol/messages.hpp"
#include "terminal/application/contracts/external/message_codec.hpp"
#include "terminal/application/contracts/external/service_socket.hpp"

namespace wpc {

struct ServiceStatus {
    ipc::Status status;
    /// status.stops as colours (those that aren't colours left out).
    std::vector<Rgb> stops;
};

/// Throws wpc::Error when the service isn't running or doesn't answer.
ServiceStatus service_status(ServiceSocket& service, MessageCodec& codec);

}  // namespace wpc
