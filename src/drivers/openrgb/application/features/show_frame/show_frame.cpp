#include "drivers/openrgb/application/features/show_frame/show_frame.hpp"

#include "drivers/openrgb/domain/zones.hpp"
#include "utils/error.hpp"
#include "utils/text.hpp"

namespace wpc::openrgb {

void show_frame(OpenRgbServer& server, const ServerState& state, size_t device, std::span<const ZoneFrame> zones) {
    if (!server.is_open()) throw Error("OpenRGB: not connected");
    const Controller& controller = state.controllers.at(device);
    bool whole = zones.size() == classify(controller).size();
    for (size_t i = 0; whole && i < zones.size(); ++i) whole = zones[i].zone == i;
    try {
        if (whole) {
            std::vector<Rgb> all;
            for (const ZoneFrame& z : zones) all.insert(all.end(), z.colours.begin(), z.colours.end());
            server.set_leds(controller.index, all);
            return;
        }
        for (const ZoneFrame& z : zones) server.set_zone_leds(controller.index, static_cast<uint32_t>(z.zone), z.colours);
    } catch (const Error& e) {
        server.close();
        throw Error(cat("cannot reach the OpenRGB server at ", state.settings.address, ": ", e.what()));
    }
}

}  // namespace wpc::openrgb
