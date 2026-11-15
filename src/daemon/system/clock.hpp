// The Clock port: the system's clocks, and waiting on them.

#pragma once

#include "daemon/service/ports.hpp"

namespace wpc {

class SystemClock final : public Clock {
public:
    TimePoint now() override;
    void wait_until(TimePoint time) override;
    uint64_t unix_seconds() override;
};

}  // namespace wpc
