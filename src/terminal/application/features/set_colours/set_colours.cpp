#include "terminal/application/features/set_colours/set_colours.hpp"

#include <algorithm>
#include <format>

#include "shared/domain/colours/extract.hpp"
#include "shared/domain/colours/paint.hpp"
#include "utils/error.hpp"

namespace wpc {

namespace {

std::vector<Rgb> colours_to_set(Pictures& pictures, const Config& config, const ColoursToSet& what, Mode mode) {
    if (what.picture) {
        return extract(pictures.read(*what.picture, picture_side(config.extract)), config.extract).stops(mode);
    }
    std::vector<Rgb> stops;
    for (const std::string& text : what.colours) {
        std::optional<Rgb> c = Rgb::parse(text);
        if (!c) throw Error(invalid_colour_message(text));
        stops.push_back(*c);
    }
    if (mode == Mode::Single) stops.resize(1);
    return stops;
}

/// Paints the chosen zones of every device of loaded driver `name`; one line per device in
/// `done`, or in `failed`.
void paint_driver(DriverModules& drivers, const std::string& name, const Config& config, Painter& painter,
                  std::vector<std::string>& done, std::vector<std::string>& failed) {
    std::vector<Device> devices = drivers.devices(name);
    for (size_t index = 0; index < devices.size(); ++index) {
        const Device& d = devices[index];
        std::vector<std::vector<Rgb>> colours;  // kept alive for the spans below
        std::vector<size_t> zones;
        for (size_t z = 0; z < d.zones.size(); ++z) {
            const Zone& zone = d.zones[z];
            if (!config.components.wants(config.components.component_of(d.name, zone.name, zone.component))) continue;
            colours.push_back(painter.zone(d.name, zone.name, zone.leds, zone.shape));
            zones.push_back(z);
        }
        if (zones.empty()) continue;
        std::vector<ZoneFrame> frame;
        for (size_t i = 0; i < zones.size(); ++i) frame.push_back(ZoneFrame{zones[i], colours[i]});
        try {
            drivers.show(name, index, frame);
            done.push_back(d.name);
        } catch (const Error& err) {
            failed.push_back(std::format("{}: {}", d.name, err.what()));
        }
    }
}

}  // namespace

SetResult set_colours(DriverModules& drivers, Pictures& pictures, const Config& config, const ColoursToSet& what) {
    Mode mode = what.mode.value_or(config.led.mode);
    SetResult result;
    result.stops = colours_to_set(pictures, config, what, mode);

    Painter painter(result.stops, mode, config.calibration);
    std::vector<std::string> problems;
    std::vector<std::string> done;
    std::vector<std::string> failed;
    for (const DriverModule& m : drivers.installed(problems)) {
        bool chosen = std::any_of(m.components.begin(), m.components.end(), [&](Component c) { return config.components.wants(c); });
        if (!chosen) continue;
        try {
            drivers.load(m.name, config.drivers.of(m.config_table));
            paint_driver(drivers, m.name, config, painter, done, failed);
        } catch (const Error& e) {
            failed.push_back(std::format("{}: {}", m.name, e.what()));
        }
        drivers.unload(m.name);
    }
    if (done.empty()) {
        std::string why;
        for (const std::string& f : failed) why += "; " + f;
        throw Error("nothing was painted" + why);
    }
    result.painted = std::move(done);
    result.not_painted = std::move(failed);
    return result;
}

}  // namespace wpc
