// What the service needs from time.

#pragma once

#include <chrono>
#include <cstdint>

namespace wpc {

using TimePoint = std::chrono::steady_clock::time_point;

class Clock {
public:
    virtual ~Clock() = default;
    /// Now, on a clock that only goes forward (for waits).
    virtual TimePoint now() = 0;
    /// Returns at `time` (at once if it has passed).
    virtual void wait_until(TimePoint time) = 0;
    /// Seconds since the Unix epoch (for `status`).
    virtual uint64_t unix_seconds() = 0;
};

}  // namespace wpc
