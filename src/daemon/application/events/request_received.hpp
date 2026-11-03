// Event: the terminal sent a request. The notification carries the line and where the answer
// goes; if the service is busy (a fade) for 2 s, `busy_answer` is sent instead.

#pragma once

#include <string>

#include "daemon/application/contracts/request_socket.hpp"
#include "daemon/application/models/notifications.hpp"

namespace wpc {

class RequestReceivedEvent {
public:
    RequestReceivedEvent(RequestSocket& socket, Notify notify, std::string busy_answer)
        : socket_(socket), notify_(std::move(notify)), busy_answer_(std::move(busy_answer)) {}

    /// Starts listening. Throws wpc::Error.
    void start();

private:
    RequestSocket& socket_;
    Notify notify_;
    std::string busy_answer_;
};

}  // namespace wpc
