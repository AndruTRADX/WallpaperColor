// What the terminal needs to talk to the service (its socket): one line out, one line back.

#pragma once

#include <string>

namespace wpc {

class ServiceSocket {
public:
    virtual ~ServiceSocket() = default;
    /// Sends `line` and returns the answer's line. Throws wpc::Error (for example when the
    /// service isn't running, or it doesn't answer in 3 s).
    virtual std::string exchange(const std::string& line) = 0;
};

}  // namespace wpc
