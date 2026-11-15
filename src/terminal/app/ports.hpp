// The terminal's ports: what it needs from outside, besides the driver modules (DriverHost) and
// picture files (PictureReader) both programs share. The real ones are in system/, wired in
// main.cpp; the tests give fakes.

#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "drivers/host/driver_host.hpp"
#include "pictures/picture_files.hpp"
#include "settings/settings.hpp"

namespace wpc {

/// The service's socket: one line out, one line back (system/service_client.hpp).
class ServiceClient {
public:
    virtual ~ServiceClient() = default;
    /// Sends `line` and returns the answer's line. Throws wpc::Error (for example when the
    /// service isn't running, or it doesn't answer in 3 s).
    virtual std::string exchange(const std::string& line) = 0;
};

/// Changing the settings file: each method one edit of its text, keeping the user's comments
/// and layout; then saving it (system/settings_editor.hpp).
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

/// The ports the commands and the terminal UI hand to the use cases, as main.cpp wires them.
struct TerminalParts {
    ServiceClient& service;
    DriverHost& drivers;
    PictureReader& pictures;
};

}  // namespace wpc
