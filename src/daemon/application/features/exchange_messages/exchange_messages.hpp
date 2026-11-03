// Feature: the terminal's requests and the service's answers, as the lines the socket carries.

#pragma once

#include <optional>
#include <string>

#include "daemon/application/contracts/external/message_codec.hpp"

namespace wpc {

struct ReadRequest {
    /// nullopt when the line is not a request; `problem` says why.
    std::optional<ipc::Request> request;
    std::string problem;
};

ReadRequest read_request(MessageCodec& codec, const std::string& line);

/// The answer's line; an empty line when it can't be written (a path that is not UTF-8).
std::string write_answer(MessageCodec& codec, const ipc::Response& answer);

}  // namespace wpc
