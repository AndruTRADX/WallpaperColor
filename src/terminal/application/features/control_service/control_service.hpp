// Feature: ask the service to do something (pause, resume, apply, the white test).

#pragma once

#include "shared/models/protocol/messages.hpp"
#include "terminal/application/contracts/external/message_codec.hpp"
#include "terminal/application/contracts/external/service_socket.hpp"

namespace wpc {

/// Sends `request`. Throws wpc::Error when the service isn't running or answers with an error.
void control_service(ServiceSocket& service, MessageCodec& codec, ipc::Request request);

/// Sends `request` and returns the answer; an error answer is thrown as wpc::Error, and so is
/// a service that isn't running (with how to start it).
ipc::Response ask_service(ServiceSocket& service, MessageCodec& codec, ipc::Request request);

}  // namespace wpc
