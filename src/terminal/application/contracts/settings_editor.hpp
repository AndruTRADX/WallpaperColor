// What the terminal needs to change the settings file: each method one edit of its text,
// keeping the user's comments and layout; then saving it.

#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "shared/domain/settings/settings.hpp"

namespace wpc {

class SettingsEditor {
public:
    virtual ~SettingsEditor() = default;

    /// The file's text as it would be saved.
    virtual std::string text() const = 0;

    // Each edit returns false when the file's layout is one it does not edit.
    virtual bool set_mode(Mode mode) = 0;
    /// The whole `[components]` table: one `name = true/false` per component.
    virtual bool set_components(const std::array<bool, ALL_COMPONENTS.size()>& chosen) = 0;
    /// `key = value` in `[table]`, the value rounded to `decimals` decimals.
    virtual bool set_float(std::string_view table, std::string_view key, double value, int decimals) = 0;
    /// `key = value` in `[table]`, a whole number.
    virtual bool set_integer(std::string_view table, std::string_view key, long value) = 0;
    /// `key = "value"` in `[table]`.
    virtual bool set_string(std::string_view table, std::string_view key, std::string_view value) = 0;
    /// Channel `channel` (0 red, 1 green, 2 blue) of calibration entry `index`, 0-1.
    virtual bool set_gain(size_t index, int channel, double value) = 0;
    /// Adds a `[[calibration]]` entry at the end with gains 1, 1, 1.
    virtual bool add_calibration(std::string_view device, std::optional<std::string_view> zone) = 0;
    /// Removes calibration entry `index`, with the comment lines just above it.
    virtual bool remove_calibration(size_t index) = 0;

    /// Writes the file atomically. Throws wpc::Error.
    virtual void save() const = 0;
};

}  // namespace wpc
