// Feature: the drivers' own settings on the Drivers page (the options each module offers, like
// the keyboard's direction), and choosing one. The choice goes into the driver's table; the
// service then creates the driver again with it.

#pragma once

#include <string>
#include <vector>

#include "shared/domain/settings/settings.hpp"
#include "terminal/application/contracts/settings_editor.hpp"
#include "terminal/application/models/driver_module.hpp"

namespace wpc {

struct DriverOptionValue {
    std::string driver;  // "razer"
    std::string table;   // "keyboard"
    ModuleOption option;
    /// What the table says, or the default (the first choice) when it says nothing.
    std::string value;
};

/// Every option of `installed`, driver by driver, with its value in `config`.
std::vector<DriverOptionValue> driver_options(const std::vector<DriverModule>& installed, const Config& config);

/// The next choice of `option` (`direction` -1: the one before), written to its table. False
/// when the file's layout can't be edited. Doesn't save.
bool cycle_driver_option(SettingsEditor& editor, const DriverOptionValue& option, int direction);

}  // namespace wpc
