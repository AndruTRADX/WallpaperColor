// Adapter: the drivers module's module catalog (drivers/host) as the service's DriverModules.
// It keeps the loaded drivers.

#pragma once

#include <map>

#include "daemon/application/contracts/external/driver_modules.hpp"
#include "drivers/host/services/module_catalog.hpp"

namespace wpc::inline daemon {

class HostDriverModules final : public DriverModules {
public:
    std::vector<DriverModule> installed(std::vector<std::string>& problems) override;
    std::vector<std::string> load(const std::string& name, const DriverSettings& settings) override;
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

}  // namespace wpc::daemon
