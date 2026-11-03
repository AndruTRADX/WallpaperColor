// Feature: choosing what is painted (`wallpapercolor drivers`, the terminal UI's Drivers page).
// The choice goes into [components]; the service then keeps in memory only the drivers of what
// is chosen.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "shared/domain/settings/settings.hpp"
#include "shared/models/protocol/messages.hpp"
#include "terminal/application/contracts/external/driver_modules.hpp"
#include "terminal/application/contracts/external/settings_file.hpp"
#include "terminal/application/contracts/settings_editor.hpp"

namespace wpc {

struct ComponentChoice {
    Component component;
    bool chosen;
    /// The installed drivers that paint it.
    std::vector<std::string> drivers;
    /// What the service does with them: one is loaded for it…
    bool loaded = false;
    /// …or the problem of one of them.
    std::optional<std::string> error;
};

/// Every component: chosen or not, which of `installed` paint it and, with the service's
/// `status`, whether it has them loaded.
std::vector<ComponentChoice> component_choices(const std::vector<DriverModule>& installed, const Config& config,
                                               const ipc::Status* status = nullptr);

struct ComponentList {
    std::vector<ComponentChoice> choices;
    /// Modules that could not be read.
    std::vector<std::string> problems;
    /// Where drivers are looked for.
    std::string location;
};

/// The components and the installed drivers (none is loaded).
ComponentList list_components(DriverModules& drivers, const Config& config);

/// Turns the components named in `names` ("fans", "ram"…) on or off, and saves. Throws
/// wpc::Error (an unknown name, a layout it can't edit, a file it can't write).
void choose_components(SettingsEditor& editor, SettingsFormat& format, const std::vector<std::string>& names, bool on);

/// Turns `component` on if it is off and off if it is on. False when the file's layout can't
/// be edited. Doesn't save.
bool toggle_component(SettingsEditor& editor, SettingsFormat& format, Component component);

}  // namespace wpc
