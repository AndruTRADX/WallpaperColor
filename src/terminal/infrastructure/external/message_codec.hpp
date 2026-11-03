// Adapter: the shared module's protocol_json service as the terminal's MessageCodec.

#pragma once

#include "shared/infrastructure/services/protocol_json.hpp"
#include "terminal/application/contracts/external/message_codec.hpp"

namespace wpc::inline terminal {

class ProtocolJsonCodec final : public MessageCodec {
public:
    std::string encode(ipc::Request request) override { return json_.encode(request); }
    ipc::Response decode_response(std::string_view line) override { return json_.decode_response(line); }

private:
    ProtocolJson json_;
};

}  // namespace wpc::terminal
