// Feature: what every installed driver finds, zone by zone (`wallpapercolor devices`). Every
// driver is loaded in the terminal, asked for its devices and unloaded again.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "shared/domain/settings/settings.hpp"
#include "terminal/application/contracts/external/driver_modules.hpp"

namespace wpc {

struct ListedZone {
    std::string name;
    uint32_t leds = 0;
    /// What it is, after [[component]] rules.
    Component component = Component::Other;
    /// Whether it is painted.
    bool chosen = false;
};

struct ListedDevice {
    std::string name;
    /// What its first LED shows now, if the device can tell.
    std::optional<Rgb> now;
    std::vector<ListedZone> zones;
};

struct ListedDriver {
    std::string name;
    /// Its table in the settings.
    std::string config_table;
    std::vector<Component> components;
    /// Why it couldn't look, if it couldn't.
    std::optional<std::string> error;
    std::vector<ListedDevice> devices;
    /// The settings the Drivers page offers for it.
    std::vector<ModuleOption> options = {};
};

struct DeviceList {
    std::vector<ListedDriver> drivers;
    /// Modules that could not be read (another build, not a driver…).
    std::vector<std::string> problems;
};

DeviceList list_devices(DriverModules& drivers, const Config& config);

}  // namespace wpc
