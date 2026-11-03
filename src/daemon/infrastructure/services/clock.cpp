#include "daemon/infrastructure/services/clock.hpp"

#include <thread>

namespace wpc {

TimePoint SystemClock::now() {
    return std::chrono::steady_clock::now();
}

void SystemClock::wait_until(TimePoint time) {
    std::this_thread::sleep_until(time);
}

uint64_t SystemClock::unix_seconds() {
    auto since = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(since).count());
}

}  // namespace wpc
