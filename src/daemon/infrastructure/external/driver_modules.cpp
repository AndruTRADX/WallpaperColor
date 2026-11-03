#include "daemon/infrastructure/external/driver_modules.hpp"

#include <algorithm>

namespace wpc {

std::vector<DriverModule> HostDriverModules::installed(std::vector<std::string>& problems) {
    found_ = catalog_.installed(problems);
    std::vector<DriverModule> out;
    for (const InstalledModule& m : found_) out.push_back(DriverModule{m.name, m.config_table, m.components});
    return out;
}

std::vector<std::string> HostDriverModules::load(const std::string& name, const DriverSettings& settings) {
    auto module = std::find_if(found_.begin(), found_.end(), [&](const InstalledModule& m) { return m.name == name; });
    if (module == found_.end()) throw Error("driver " + name + " is not installed");
    LoadedModule created = catalog_.create(catalog_.load(*module), settings);
    loaded_[name] = std::move(created.driver);
    return created.unread_settings;
}

void HostDriverModules::unload(const std::string& name) {
    loaded_.erase(name);  // the driver, then its module
}

std::vector<Device> HostDriverModules::devices(const std::string& name) {
    return loaded_.at(name)->devices();
}

void HostDriverModules::show(const std::string& name, size_t device, std::span<const ZoneFrame> zones) {
    loaded_.at(name)->show(device, zones);
}

}  // namespace wpc
