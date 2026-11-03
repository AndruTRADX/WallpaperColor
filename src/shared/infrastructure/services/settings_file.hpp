// Service: the settings file, ~/.config/wallpapercolor/config.toml, read with toml++.
//
// Unknown keys and wrong types are errors (a typo should not silently do nothing). Tables that
// aren't WallpaperColor's own ([led], [extract], [components]…) are kept for the drivers.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "shared/domain/settings/settings.hpp"

namespace wpc {

class SettingsFile {
public:
    /// $XDG_CONFIG_HOME/wallpapercolor/config.toml (or ~/.config/...); nullopt without HOME.
    std::optional<std::filesystem::path> default_path() const;

    /// Reads the file; a missing file gives the defaults. Throws wpc::Error.
    Config read(const std::filesystem::path& path) const;

    /// The settings in TOML text. Throws wpc::Error (the message has no file name).
    Config parse(std::string_view text) const;

    /// The whole settings as TOML, defaults included (what `wallpapercolor config` prints).
    std::string write(const Config& config) const;
};

}  // namespace wpc
