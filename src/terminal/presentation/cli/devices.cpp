// `devices`: what every installed driver finds, zone by zone.

#include <cstdlib>
#include <format>

#include "terminal/application/features/list_devices/list_devices.hpp"
#include "terminal/presentation/cli/commands.hpp"
#include "terminal/presentation/cli/output.hpp"

namespace wpc {

int run_devices(TerminalParts& parts, const Config& config) {
    DeviceList list = list_devices(parts.drivers, config);
    for (const std::string& p : list.problems) out("problem: " + p);
    if (list.drivers.empty()) out("no driver modules installed: run install.sh again");
    for (const ListedDriver& driver : list.drivers) {
        std::vector<std::string> components;
        for (Component c : driver.components) components.emplace_back(component_name(c));
        out(std::format("{} ({})", driver.name, join(components, ", ")));
        if (driver.error) out("  " + *driver.error);
        if (!driver.error && driver.devices.empty()) out("  no devices found");
        for (const ListedDevice& d : driver.devices) {
            out(std::format("  {}{}", d.now ? swatch(*d.now, 2) : "", d.name));
            for (const ListedZone& z : d.zones) {
                out(std::format("      {:<28} {:>3} LEDs  {}{}", z.name.empty() ? "(all)" : z.name, z.leds,
                                component_name(z.component), z.chosen ? "" : " (off)"));
            }
        }
    }
    return EXIT_SUCCESS;
}

}  // namespace wpc
