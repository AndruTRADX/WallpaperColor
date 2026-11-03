#include "daemon/application/features/exchange_messages/exchange_messages.hpp"

#include "utils/error.hpp"

namespace wpc {

ReadRequest read_request(MessageCodec& codec, const std::string& line) {
    try {
        return ReadRequest{codec.decode_request(line), {}};
    } catch (const Error& e) {
        return ReadRequest{std::nullopt, std::string("bad request: ") + e.what()};
    }
}

std::string write_answer(MessageCodec& codec, const ipc::Response& answer) {
    try {
        return codec.encode(answer);
    } catch (const std::exception&) {
        return "";
    }
}

}  // namespace wpc
