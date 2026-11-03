#include "daemon/application/events/request_received.hpp"

#include <chrono>

namespace wpc {

void RequestReceivedEvent::start() {
    socket_.serve([this](const std::string& line) {
        auto reply = std::make_shared<Channel<std::string>>();
        notify_(RequestNotice{line, reply});
        return reply->receive_for(std::chrono::seconds(2)).value_or(busy_answer_);
    });
}

}  // namespace wpc
