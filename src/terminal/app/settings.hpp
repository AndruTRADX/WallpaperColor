// The terminal's use cases about the settings: reading them, showing them, changing them from
// the terminal UI or a command (the mode, the numbers, calibrations, what is painted, the
// drivers' own settings) and saving them. Every change goes through the SettingsEditor, which
// keeps the user's comments; the service notices the file change and applies it.
// Every edit returns false when the file's layout is one the editor can't change.

#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "protocol/messages.hpp"
#include "settings/settings.hpp"
#include "terminal/app/ports.hpp"

namespace wpc {

// ---------------------------------------------------------------------------------------------
// Reading, showing, saving

struct LoadedSettings {
    /// nullopt when there is no file to use (no HOME).
    std::optional<std::filesystem::path> path;
    Config config;
};

/// The settings from the file chosen on the command line, or the default one. Throws
/// wpc::Error for a mistake in the file.
LoadedSettings load_settings(const std::optional<std::filesystem::path>& chosen);

/// The settings the editor's text gives, as the service will read them (the defaults when it
/// has a mistake).
Config edited_settings(const SettingsEditor& editor);

/// The settings in use as the file would have them, defaults included (`wallpapercolor
/// config`), without trailing blank lines.
std::string show_settings(const Config& config);

struct SavedSettings {
    /// The settings now (also when saving failed: they are what the editor has).
    Config config;
    /// Why the file couldn't be written.
    std::optional<std::string> error;
    /// The settings colours are taken with changed: a preview of them must be taken again.
    bool extract_changed = false;
};

/// Saves the edited settings. `before`: the settings before the edit.
SavedSettings save_settings(SettingsEditor& editor, const Config& before);

// ---------------------------------------------------------------------------------------------
// The terminal UI's settings

/// A setting that is a number, changed in steps: where it is and its range (within the range
/// the file allows, checked when this compiles). How it is shown is the terminal UI's.
struct NumberSetting {
    const char* table;
    const char* key;
    double step;
    double min;
    double max;
    int decimals;  // -1: a whole number
    double (*get)(const Config&);
};

/// Every number setting, in the order the terminal UI shows them.
std::span<const NumberSetting> number_settings();

/// arc → single → split → arc (`direction` -1 goes the other way).
bool cycle_mode(SettingsEditor& editor, int direction);

/// One step of number setting `index` up (+1) or down (-1), within its range.
bool step_number(SettingsEditor& editor, size_t index, int direction);

/// One step (0.05) of channel `channel` (0 red, 1 green, 2 blue) of calibration `entry`.
bool step_gain(SettingsEditor& editor, size_t entry, int channel, int direction);

/// A device, or one zone of it, that a new calibration can be for.
struct CalibrationTarget {
    std::string device;
    std::optional<std::string> zone;
};

/// Every device and zone of `devices` a calibration can be for, each name once (two RAM
/// sticks are one target).
std::vector<CalibrationTarget> calibration_targets(const std::vector<ipc::DeviceInfo>& devices);

bool add_calibration(SettingsEditor& editor, const CalibrationTarget& target);
bool remove_calibration(SettingsEditor& editor, size_t entry);

// ---------------------------------------------------------------------------------------------
// What is painted (`wallpapercolor drivers`, the Drivers page). The choice goes into
// [components]; the service then keeps in memory only the drivers of what is chosen.

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
ComponentList list_components(DriverHost& drivers, const Config& config);

/// Turns the components named in `names` ("fans", "ram"…) on or off, and saves. Throws
/// wpc::Error (an unknown name, a layout it can't edit, a file it can't write).
void choose_components(SettingsEditor& editor, const std::vector<std::string>& names, bool on);

/// Turns `component` on if it is off and off if it is on. Doesn't save.
bool toggle_component(SettingsEditor& editor, Component component);

// ---------------------------------------------------------------------------------------------
// The drivers' own settings on the Drivers page (the options each module offers, like the
// keyboard's direction). The choice goes into the driver's table; the service then creates the
// driver again with it.

struct DriverOptionValue {
    std::string driver;  // "razer"
    std::string table;   // "keyboard"
    ModuleOption option;
    /// What the table says, or the default (the first choice) when it says nothing.
    std::string value;
};

/// Every option of `installed`, driver by driver, with its value in `config`.
std::vector<DriverOptionValue> driver_options(const std::vector<DriverModule>& installed, const Config& config);

/// The next choice of `option` (`direction` -1: the one before), written to its table. Doesn't
/// save.
bool cycle_driver_option(SettingsEditor& editor, const DriverOptionValue& option, int direction);

}  // namespace wpc
