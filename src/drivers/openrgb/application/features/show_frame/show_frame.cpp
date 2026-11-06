#include "drivers/openrgb/application/features/show_frame/show_frame.hpp"

#include "utils/error.hpp"
#include "utils/text.hpp"

namespace wpc::openrgb {

namespace {

/// The zones' colours as one span when each zone's follow the previous one's in memory (the
/// service sends every frame as one buffer); empty otherwise.
std::span<const Rgb> one_span(std::span<const ZoneFrame> zones) {
    if (zones.empty()) return {};
    const Rgb* end = zones.front().colours.data();
    for (const ZoneFrame& z : zones) {
        if (z.colours.data() != end) return {};
        end += z.colours.size();
    }
    return std::span(zones.front().colours.data(), end);
}

}  // namespace

void show_frame(OpenRgbServer& server, const ServerState& state, size_t device, std::span<const ZoneFrame> zones) {
    if (!server.is_open()) throw Error("OpenRGB: not connected");
    const Controller& controller = state.controllers.at(device);
    bool whole = zones.size() == state.zone_counts.at(device);
    for (size_t i = 0; whole && i < zones.size(); ++i) whole = zones[i].zone == i;
    try {
        if (whole) {
            std::span<const Rgb> all = one_span(zones);
            std::vector<Rgb> copied;
            if (all.empty()) {
                for (const ZoneFrame& z : zones) copied.insert(copied.end(), z.colours.begin(), z.colours.end());
                all = copied;
            }
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
