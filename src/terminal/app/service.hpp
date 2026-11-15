// The terminal's use cases about the service: what it is doing (`wallpapercolor status`, the
// terminal UI's header) and asking it to do something (pause, resume, apply, the white test).

#pragma once

#include <vector>

#include "protocol/messages.hpp"
#include "terminal/app/ports.hpp"

namespace wpc {

/// Sends `request` and returns the answer; an error answer is thrown as wpc::Error, and so is
/// a service that isn't running (with how to start it).
ipc::Response ask_service(ServiceClient& service, ipc::Request request);

/// Sends `request`. Throws wpc::Error when the service isn't running or answers with an error.
void control_service(ServiceClient& service, ipc::Request request);

struct ServiceStatus {
    ipc::Status status;
    /// status.stops as colours (those that aren't colours left out).
    std::vector<Rgb> stops;
};

/// Throws wpc::Error when the service isn't running or doesn't answer.
ServiceStatus service_status(ServiceClient& service);

}  // namespace wpc
