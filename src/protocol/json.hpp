// The protocol's messages (messages.hpp) as JSON lines, with nlohmann/json. Fields keep a fixed
// order; missing optional values are `null`. One line, no trailing newline. Pure functions.

#pragma once

#include <string>
#include <string_view>

#include "protocol/messages.hpp"

namespace wpc::ipc {

std::string encode(Request request);

/// Text that is not valid UTF-8 (a picture's path can be any bytes) is sent with U+FFFD in
/// place of the bad bytes, so the answer always goes out.
std::string encode(const Response& response);

/// Throws wpc::Error on bad input.
Request decode_request(std::string_view line);

/// Throws wpc::Error on bad input.
Response decode_response(std::string_view line);

}  // namespace wpc::ipc
