#include "terminal/application/features/service_status/service_status.hpp"

#include "terminal/application/features/control_service/control_service.hpp"
#include "utils/error.hpp"

namespace wpc {

ServiceStatus service_status(ServiceSocket& service, MessageCodec& codec) {
    ipc::Response response = ask_service(service, codec, ipc::Request::Status);
    if (response.kind != ipc::Response::Kind::Status) throw Error("the service gave no status");
    ServiceStatus out{std::move(response.status), {}};
    for (const std::string& hex : out.status.stops) {
        if (std::optional<Rgb> c = Rgb::parse(hex)) out.stops.push_back(*c);
    }
    return out;
}

}  // namespace wpc
