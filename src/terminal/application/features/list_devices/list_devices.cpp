#include "terminal/application/features/list_devices/list_devices.hpp"

#include "utils/error.hpp"

namespace wpc {

DeviceList list_devices(DriverModules& drivers, const Config& config) {
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

}  // namespace wpc
