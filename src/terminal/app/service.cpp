#include "terminal/app/service.hpp"

#include <format>

#include "colours/colour.hpp"
#include "protocol/json.hpp"
#include "utils/error.hpp"

namespace wpc {

ipc::Response ask_service(ServiceClient& service, ipc::Request request) {
    std::string answer;
    try {
        answer = service.exchange(ipc::encode(request));
    } catch (const Error& e) {
        throw Error(std::format("the service is not running ({}): systemctl --user start wallpapercolord", e.what()));
    }
    ipc::Response response = ipc::decode_response(answer);
    if (response.kind == ipc::Response::Kind::Error) throw Error(response.message);
    return response;
}

void control_service(ServiceClient& service, ipc::Request request) {
    ask_service(service, request);
}

ServiceStatus service_status(ServiceClient& service) {
    ipc::Response response = ask_service(service, ipc::Request::Status);
    if (response.kind != ipc::Response::Kind::Status) throw Error("the service gave no status");
    ServiceStatus out{std::move(response.status), {}};
    for (const std::string& hex : out.status.stops) {
        if (std::optional<Rgb> c = parse_colour(hex)) out.stops.push_back(*c);
    }
    return out;
}

}  // namespace wpc
