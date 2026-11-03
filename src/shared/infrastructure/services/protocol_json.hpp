// Service: the protocol's messages (shared/models/protocol) as JSON lines, with nlohmann/json.
// Fields keep a fixed order; missing optional values are `null`. One line, no trailing newline.

#pragma once

#include <string>
#include <string_view>

#include "shared/models/protocol/messages.hpp"

namespace wpc {

class ProtocolJson {
public:
    std::string encode(ipc::Request request) const;
    /// Throws wpc::Error on text that is not valid UTF-8 (a picture's path, for example).
    std::string encode(const ipc::Response& response) const;
    /// Throws wpc::Error on bad input.
    ipc::Request decode_request(std::string_view line) const;
    /// Throws wpc::Error on bad input.
    ipc::Response decode_response(std::string_view line) const;
};

}  // namespace wpc
