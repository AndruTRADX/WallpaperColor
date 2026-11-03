// What the service needs to hear the terminal: lines in, lines out.

#pragma once

#include <functional>
#include <string>

namespace wpc {

class RequestSocket {
public:
    virtual ~RequestSocket() = default;
    /// Listens (on a thread of its own) and answers every request line with `answer`'s line.
    /// Throws wpc::Error when it can't listen.
    virtual void serve(std::function<std::string(const std::string& request)> answer) = 0;
};

}  // namespace wpc
