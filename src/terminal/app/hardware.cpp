#include "terminal/app/hardware.hpp"

#include <algorithm>
#include <format>

#include "colours/colour.hpp"
#include "colours/paint.hpp"
#include "utils/error.hpp"

namespace wpc {

// ---------------------------------------------------------------------------------------------
// What every installed driver finds

DeviceList list_devices(DriverHost& drivers, const Config& config) {
    DeviceList out;
    for (const DriverModule& m : drivers.installed(out.problems)) {
        ListedDriver driver{m.name, m.config_table, m.components, std::nullopt, {}, m.options};
        std::vector<Device> devices;
        try {
            drivers.load(m.name, config.drivers.of(m.config_table));
            devices = drivers.devices(m.name);
        } catch (const Error& e) {
            driver.error = e.what();
        }
        drivers.unload(m.name);
        for (const Device& d : devices) {
            ListedDevice device{d.name, d.reported.empty() ? std::nullopt : std::optional(d.reported[0]), {}};
            for (const Zone& z : d.zones) {
                Component c = config.components.component_of(d.name, z.name, z.component);
                device.zones.push_back(ListedZone{z.name, z.leds, c, config.components.wants(c)});
            }
            driver.devices.push_back(std::move(device));
        }
        out.drivers.push_back(std::move(driver));
    }
    return out;
}

Detection detect_hardware(DriverHost& drivers, const Config& config) {
    Detection out;
    DeviceList list = list_devices(drivers, config);
    out.problems = list.problems;
    for (const ListedDriver& driver : list.drivers) {
        out.modules.push_back(DriverModule{driver.name, driver.config_table, driver.components, driver.options});
        if (driver.error) out.problems.push_back(std::format("{}: {}", driver.name, *driver.error));
        for (Component c : ALL_COMPONENTS) {
            std::vector<std::string>& found = out.found[static_cast<size_t>(c)];
            for (const ListedDevice& d : driver.devices) {
                std::vector<std::string> zones;
                for (const ListedZone& z : d.zones) {
                    if (z.component == c) zones.push_back(z.name);
                }
                if (zones.empty()) continue;
                found.push_back(zones.size() == d.zones.size() ? d.name
                                : zones.size() == 1           ? std::format("{} · {}", d.name, zones[0])
                                                              : std::format("{}: {} zones", d.name, zones.size()));
            }
        }
    }
    // The same thing twice (two RAM sticks): "VENGEANCE RGB × 2".
    for (std::vector<std::string>& found : out.found) {
        std::vector<std::string> merged;
        for (const std::string& f : found) {
            if (std::find(merged.begin(), merged.end(), f) != merged.end()) continue;
            long n = std::count(found.begin(), found.end(), f);
            merged.push_back(n > 1 ? std::format("{} × {}", f, n) : f);
        }
        found = merged;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Painting once

namespace {

std::vector<Rgb> colours_to_set(PictureReader& pictures, const Config& config, const ColoursToSet& what, Mode mode) {
    if (what.picture) {
        return extract(pictures.read(*what.picture, picture_side(config.extract)), config.extract).stops(mode);
    }
    std::vector<Rgb> stops;
    for (const std::string& text : what.colours) {
        std::optional<Rgb> c = parse_colour(text);
        if (!c) throw Error(invalid_colour_message(text));
        stops.push_back(*c);
    }
    if (mode == Mode::Single) stops.resize(1);
    return stops;
}

/// Paints the chosen zones of every device of loaded driver `name`; one line per device in
/// `done`, or in `failed`.
void paint_driver(DriverHost& drivers, const std::string& name, const Config& config, Painter& painter,
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

SetResult set_colours(DriverHost& drivers, PictureReader& pictures, const Config& config, const ColoursToSet& what) {
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

// ---------------------------------------------------------------------------------------------
// Colours of pictures, and the preview

std::vector<PictureColours> extract_colours(PictureReader& files, const std::vector<std::filesystem::path>& pictures,
                                           const ExtractConfig& config) {
    std::vector<PictureColours> out;
    for (const std::filesystem::path& picture : pictures) {
        PictureColours result{picture, std::nullopt, {}};
        try {
            result.colours = extract(files.read(picture, picture_side(config)), config);
        } catch (const Error& e) {
            result.error = e.what();
        }
        out.push_back(std::move(result));
    }
    return out;
}

std::vector<Rgb> preview_colours(const std::optional<Extraction>& local, Mode mode, const std::vector<Rgb>& service_stops) {
    return local ? local->stops(mode) : service_stops;
}

std::vector<PreviewDevice> preview_devices(const std::vector<ipc::DeviceInfo>& devices, const std::vector<Rgb>& stops,
                                           bool testing, const Config& config) {
    Painter painter(testing ? std::vector<Rgb>{WHITE} : stops, testing ? Mode::Single : config.led.mode,
                    config.calibration);
    std::vector<PreviewDevice> out;
    for (const ipc::DeviceInfo& d : devices) {
        PreviewDevice device{d.name, {}};
        for (const ipc::ZoneInfo& z : d.zones) {
            PreviewZone zone{z, config.components.wants(z.component), {}, false};
            if (zone.chosen) {
                zone.leds = painter.zone(d.name, z.name, z.leds, z.shape);
                zone.calibrated = config.calibration.gain(d.name, z.name) != std::array<float, 3>{1, 1, 1};
            }
            device.zones.push_back(std::move(zone));
        }
        out.push_back(std::move(device));
    }
    return out;
}

}  // namespace wpc
