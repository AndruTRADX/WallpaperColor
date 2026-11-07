#include "daemon/infrastructure/services/logind.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include "utils/error.hpp"
#include "utils/event_loop.hpp"

namespace wpc {

Logind::Logind(EventLoop& loop) : loop_(loop) {}

Logind::~Logind() {
    if (bus_fds_) remove_bus(loop_, *bus_fds_);
}

void Logind::listen(std::function<void()> woke) {
    try {
        std::unique_ptr<sdbus::IConnection> connection = sdbus::createSystemBusConnection();
        auto on_message = [woke](sdbus::Message message) {
            bool going_to_sleep = true;
            try {
                message >> going_to_sleep;
            } catch (const sdbus::Error&) {
                return;
            }
            if (!going_to_sleep) woke();
        };
        connection->addMatch(
            "type='signal',sender='org.freedesktop.login1',interface='org.freedesktop.login1.Manager',"
            "member='PrepareForSleep'",
            on_message);
        bus_fds_ = add_bus(loop_, *connection);
        connection_ = std::move(connection);
    } catch (const sdbus::Error& e) {
        throw Error(e.what());
    }
}

}  // namespace wpc
