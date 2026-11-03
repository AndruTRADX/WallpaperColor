// What the service needs from the driver modules (the drivers module, drivers/host): which are
// installed, loading and unloading them, and talking to a loaded one.

#pragma once

#include <span>
#include <string>
#include <vector>

#include "daemon/application/models/driver_module.hpp"
#include "drivers/sdk/driver.hpp"

namespace wpc::inline daemon {

class DriverModules {
public:
    virtual ~DriverModules() = default;
    /// Every installed driver, by name; those that can't be used are left out, with the reason
    /// in `problems`.
    virtual std::vector<DriverModule> installed(std::vector<std::string>& problems) = 0;
    /// Loads driver `name` and creates it from `settings`; returns the keys it didn't read.
    /// Throws wpc::Error.
    virtual std::vector<std::string> load(const std::string& name, const DriverSettings& settings) = 0;
    /// Takes driver `name` out of memory.
    virtual void unload(const std::string& name) = 0;
    /// The devices of loaded driver `name`. Throws wpc::Error.
    virtual std::vector<Device> devices(const std::string& name) = 0;
    /// One frame on device `device` of loaded driver `name`. Throws wpc::Error.
    virtual void show(const std::string& name, size_t device, std::span<const ZoneFrame> zones) = 0;
    /// Where drivers are looked for, for messages.
    virtual std::string location() = 0;
};

}  // namespace wpc::daemon
