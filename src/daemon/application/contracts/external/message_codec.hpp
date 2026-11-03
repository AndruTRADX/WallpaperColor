// What the service needs to read the terminal's requests and write its answers (the shared
// module's protocol_json service).

#pragma once

#include <string>
#include <string_view>

#include "shared/models/protocol/messages.hpp"

namespace wpc::inline daemon {

class MessageCodec {
public:
    virtual ~MessageCodec() = default;
    /// Throws wpc::Error on bad input.
    virtual ipc::Request decode_request(std::string_view line) = 0;
    /// Throws wpc::Error on text that is not valid UTF-8.
    virtual std::string encode(const ipc::Response& response) = 0;
};

}  // namespace wpc::daemon
