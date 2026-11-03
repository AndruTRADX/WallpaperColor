// Adapter: the drivers module's module catalog (drivers/host) as the terminal's DriverModules.
// It keeps the loaded drivers.

#pragma once

#include <map>
#include <memory>

#include "drivers/host/services/module_catalog.hpp"
#include "terminal/application/contracts/external/driver_modules.hpp"

namespace wpc::inline terminal {

class HostDriverModules final : public DriverModules {
public:
    std::vector<DriverModule> installed(std::vector<std::string>& problems) override;
    void load(const std::string& name, const DriverSettings& settings) override;
    void unload(const std::string& name) override;
    std::vector<Device> devices(const std::string& name) override;
    void show(const std::string& name, size_t device, std::span<const ZoneFrame> zones) override;
    std::string location() override { return catalog_.location(); }

private:
    ModuleCatalog catalog_;
    /// From the last installed(): where each module's file is.
    std::vector<InstalledModule> found_;
    std::map<std::string, std::unique_ptr<Driver>> loaded_;
};

}  // namespace wpc::terminal
