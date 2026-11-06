#include "daemon/infrastructure/services/logind.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include "utils/error.hpp"

namespace wpc {

Logind::Logind() = default;

Logind::~Logind() = default;

void Logind::listen(std::function<void()> woke) {
    try {
        connection_ = sdbus::createSystemBusConnection();
        auto on_message = [woke](sdbus::Message message) {
            bool going_to_sleep = true;
            try {
                message >> going_to_sleep;
            } catch (const sdbus::Error&) {
                return;
            }
            if (!going_to_sleep) woke();
        };
        connection_->addMatch(
            "type='signal',sender='org.freedesktop.login1',interface='org.freedesktop.login1.Manager',"
            "member='PrepareForSleep'",
            on_message);
        connection_->enterEventLoopAsync();
    } catch (const sdbus::Error& e) {
        throw Error(e.what());
    }
}

}  // namespace wpc
