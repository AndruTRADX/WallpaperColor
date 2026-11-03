// Feature: apply the settings. Reads them (the defaults when they have a mistake), then loads
// the drivers the chosen components need and unloads the others. A loaded driver whose table
// didn't change is kept, with its connection.

#pragma once

#include "daemon/application/contracts/external/driver_modules.hpp"
#include "daemon/application/contracts/external/settings_source.hpp"
#include "daemon/application/contracts/memory.hpp"
#include "daemon/application/models/service_state.hpp"

namespace wpc {

/// `first`: when the service starts (only the log line differs).
void apply_settings(ServiceState& state, SettingsSource& settings, DriverModules& drivers, Memory& memory,
                    bool first);

}  // namespace wpc
