// Edits the config file in place, keeping the user's comments and layout.
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

#include "core/config.hpp"

namespace wpc {

class Settings {
public:
    /// Reads `path`; a missing file starts empty. Throws wpc::Error if it is not valid TOML.
    static Settings load(const std::filesystem::path& path);

    /// Settings over `text` without a file (for tests).
    static Settings from_text(std::string_view text);

    /// The config as the service will read it (defaults if the file has a mistake).
    Config config() const;

    /// The file's text as it would be saved.
    std::string text() const;

    // Each setter returns false when the file's layout is one it does not edit (see above).
    bool set_mode(Mode mode);
    /// min_saturation, kept between 0.2 and 1, rounded to two decimals.
    bool set_intensity(double value);
    /// Channel `channel` (0 red, 1 green, 2 blue) of calibration entry `index`, 0-1.
    /// An entry that does not exist is ignored (returns true).
    bool set_gain(size_t index, int channel, double value);

    /// Writes the file atomically (the service never reads a half-written file). Throws wpc::Error.
    void save() const;

private:
    /// Puts `value` in place of the value of the key on `line`. False if it cannot be edited.
    bool replace_value(size_t line, size_t value_start, const std::string& value);
    /// Sets `key = value` in table `[table]`, adding the key or table when missing.
    bool set_value(std::string_view table, std::string_view key, const std::string& value);

    std::filesystem::path path_;
    std::vector<std::string> lines_;
};

}  // namespace wpc
