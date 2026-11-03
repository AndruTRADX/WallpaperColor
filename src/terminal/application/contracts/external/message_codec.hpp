// What the terminal needs to write its requests and read the service's answers (the shared
// module's protocol_json service).

#pragma once

#include <string>
#include <string_view>

#include "shared/models/protocol/messages.hpp"

namespace wpc::inline terminal {

class MessageCodec {
public:
    virtual ~MessageCodec() = default;
    virtual std::string encode(ipc::Request request) = 0;
    /// Throws wpc::Error on bad input.
    virtual ipc::Response decode_response(std::string_view line) = 0;
};

}  // namespace wpc::terminal
