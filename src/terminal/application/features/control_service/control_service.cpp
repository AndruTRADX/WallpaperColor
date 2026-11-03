#include "terminal/application/features/control_service/control_service.hpp"

#include <format>

#include "utils/error.hpp"

namespace wpc {

ipc::Response ask_service(ServiceSocket& service, MessageCodec& codec, ipc::Request request) {
    std::string answer;
    try {
        answer = service.exchange(codec.encode(request));
    } catch (const Error& e) {
        throw Error(std::format("the service is not running ({}): systemctl --user start wallpapercolord", e.what()));
    }
    ipc::Response response = codec.decode_response(answer);
    if (response.kind == ipc::Response::Kind::Error) throw Error(response.message);
    return response;
}

void control_service(ServiceSocket& service, MessageCodec& codec, ipc::Request request) {
    ask_service(service, codec, request);
}

}  // namespace wpc
