// Feature: changing settings from the terminal UI: the mode, the numbers, calibrations. Each
// change is written by the SettingsEditor (keeping the user's comments); save_settings saves.
// Every function returns false when the file's layout can't be edited.

#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "shared/domain/settings/settings.hpp"
#include "shared/models/protocol/messages.hpp"
#include "terminal/application/contracts/external/settings_file.hpp"
#include "terminal/application/contracts/settings_editor.hpp"

namespace wpc {

/// A setting that is a number, changed in steps: where it is and its range. How it is shown
/// (label, unit, hint) is the terminal UI's.
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
bool cycle_mode(SettingsEditor& editor, SettingsFormat& format, int direction);

/// One step of number setting `index` up (+1) or down (-1), within its range.
bool step_number(SettingsEditor& editor, SettingsFormat& format, size_t index, int direction);

/// One step (0.05) of channel `channel` (0 red, 1 green, 2 blue) of calibration `entry`.
bool step_gain(SettingsEditor& editor, SettingsFormat& format, size_t entry, int channel, int direction);

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

}  // namespace wpc
