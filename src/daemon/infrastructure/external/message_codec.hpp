// Adapter: the shared module's protocol_json service as the service's MessageCodec.

#pragma once

#include "daemon/application/contracts/external/message_codec.hpp"
#include "shared/infrastructure/services/protocol_json.hpp"

namespace wpc::inline daemon {

class ProtocolJsonCodec final : public MessageCodec {
public:
    ipc::Request decode_request(std::string_view line) override { return json_.decode_request(line); }
    std::string encode(const ipc::Response& response) override { return json_.encode(response); }

private:
    ProtocolJson json_;
};

}  // namespace wpc::daemon
