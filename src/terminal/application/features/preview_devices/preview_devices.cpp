#include "terminal/application/features/preview_devices/preview_devices.hpp"

#include <array>

#include "shared/domain/colours/paint.hpp"

namespace wpc {

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
