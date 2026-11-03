#include "terminal/application/features/detect_hardware/detect_hardware.hpp"

#include <algorithm>
#include <format>

#include "terminal/application/features/list_devices/list_devices.hpp"

namespace wpc {

Detection detect_hardware(DriverModules& drivers, const Config& config) {
    Detection out;
    DeviceList list = list_devices(drivers, config);
    out.problems = list.problems;
    for (const ListedDriver& driver : list.drivers) {
        out.modules.push_back(DriverModule{driver.name, driver.config_table, driver.components});
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

}  // namespace wpc
