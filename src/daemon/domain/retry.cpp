#include "daemon/domain/retry.hpp"

#include <algorithm>

namespace wpc {

using namespace std::chrono_literals;

RetryDelay next_retry_delay(std::optional<RetryDelay> last) {
    if (!last) return 2s;
    return std::min<RetryDelay>(*last * 2, 60s);
}

}  // namespace wpc
