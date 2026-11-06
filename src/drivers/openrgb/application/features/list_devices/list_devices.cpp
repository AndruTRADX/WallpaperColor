#include "drivers/openrgb/application/features/list_devices/list_devices.hpp"

#include "drivers/openrgb/domain/zones.hpp"
#include "utils/error.hpp"
#include "utils/text.hpp"

namespace wpc::openrgb {

std::vector<Device> list_devices(OpenRgbServer& server, ServerState& state) {
    try {
        if (!server.is_open()) server.open(state.settings.address, state.settings.client_name);
        state.controllers = server.controllers();
    } catch (const Error& e) {
        server.close();
        throw Error(cat("cannot reach the OpenRGB server at ", state.settings.address, ": ", e.what()));
    }
    std::vector<Device> out;
    state.zone_counts.clear();
    for (const Controller& c : state.controllers) {
        out.push_back(Device{cat(c.index, " ", c.name), c.name, classify(c), c.colors});
        state.zone_counts.push_back(out.back().zones.size());
    }
    return out;
}

}  // namespace wpc::openrgb
