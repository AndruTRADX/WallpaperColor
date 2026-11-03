#include "daemon/domain/outputs.hpp"

namespace wpc {

void plan_outputs(const std::string& driver, const std::vector<Device>& devices, const ComponentSettings& components,
                  Painter& painter, std::vector<Output>& outputs, std::vector<ipc::DeviceInfo>& found) {
    for (size_t index = 0; index < devices.size(); ++index) {
        const Device& d = devices[index];
        ipc::DeviceInfo info{d.name, driver, {}};
        Output o{driver, index, driver + " " + d.key, {}, {}, {}};
        size_t first = 0;  // the zone's first LED in d.reported
        for (size_t z = 0; z < d.zones.size(); ++z) {
            const Zone& zone = d.zones[z];
            Component c = components.component_of(d.name, zone.name, zone.component);
            info.zones.push_back(ipc::ZoneInfo{zone.name, zone.leds, c, zone.shape});
            if (components.wants(c)) {
                std::vector<Rgb> leds = painter.zone(d.name, zone.name, zone.leds, zone.shape);
                o.zones.emplace_back(z, leds.size());
                o.target.insert(o.target.end(), leds.begin(), leds.end());
                if (d.reported.size() >= first + zone.leds) {
                    auto from = d.reported.begin() + static_cast<std::ptrdiff_t>(first);
                    o.reported.insert(o.reported.end(), from, from + zone.leds);
                }
            }
            first += zone.leds;
        }
        found.push_back(std::move(info));
        if (o.zones.empty()) continue;
        if (o.reported.size() != o.target.size()) o.reported.clear();
        outputs.push_back(std::move(o));
    }
}

}  // namespace wpc
