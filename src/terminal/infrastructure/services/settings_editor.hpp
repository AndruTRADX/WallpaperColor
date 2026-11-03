// Service: edits the config file in place, keeping the user's comments and layout.
// The service notices the file change and reloads it.
//
// No C++ TOML library keeps comments when it writes a file, so this edits the lines itself.
// It understands the normal layout (the one `wallpapercolor config` prints and the README shows):
//
//   [led]                       [extract]                 [[calibration]]
//   mode = "arc"   # comment    min_saturation = 0.7      gain = [0.5, 0.5, 1.0]  # comment
//
// A value is replaced whole (a `gain` array over several lines becomes one line) and what
// follows it, such as a comment, is kept. For other layouts (dotted keys like `led.mode = …`,
// inline tables) a setter returns false and changes nothing: edit that one in the file.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "terminal/application/contracts/settings_editor.hpp"

namespace wpc {

class TomlSettingsEditor final : public SettingsEditor {
public:
    /// Reads `path`; a missing file starts empty. Throws wpc::Error if it is not valid TOML.
    static TomlSettingsEditor open(const std::filesystem::path& path);
    /// An editor over `text` without a file (for tests).
    static TomlSettingsEditor from_text(std::string_view text);

    std::string text() const override;
    bool set_mode(Mode mode) override;
    bool set_components(const std::array<bool, ALL_COMPONENTS.size()>& chosen) override;
    bool set_float(std::string_view table, std::string_view key, double value, int decimals) override;
    bool set_integer(std::string_view table, std::string_view key, long value) override;
    /// An entry that does not exist is ignored (returns true).
    bool set_gain(size_t index, int channel, double value) override;
    /// No `zone`: every zone.
    bool add_calibration(std::string_view device, std::optional<std::string_view> zone) override;
    bool remove_calibration(size_t index) override;
    /// The service never reads a half-written file.
    void save() const override;

private:
    /// Puts `value` in place of the value of the key on `line`. False if it cannot be edited.
    bool replace_value(size_t line, size_t value_start, const std::string& value);
    /// Sets `key = value` in table `[table]`, adding the key or table when missing.
    bool set_value(std::string_view table, std::string_view key, const std::string& value);

    std::filesystem::path path_;
    std::vector<std::string> lines_;
};

}  // namespace wpc
