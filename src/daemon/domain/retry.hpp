// How long to wait before trying again after a problem: 2 s, then twice as long each time,
// up to 60 s.

#pragma once

#include <chrono>
#include <optional>

namespace wpc {

using RetryDelay = std::chrono::steady_clock::duration;

/// The wait after `last` (nullopt: the first problem).
RetryDelay next_retry_delay(std::optional<RetryDelay> last);

}  // namespace wpc
