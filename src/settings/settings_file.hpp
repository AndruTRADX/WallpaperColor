// The settings file, ~/.config/wallpapercolor/config.toml: TOML text ↔ Config (pure), and
// reading it.
//
// Unknown keys, wrong types, numbers that are not finite or out of their range (settings.hpp)
// are errors with the line and column: a typo must not silently do nothing, and a value the
// service can't use must not reach it. Tables that aren't WallpaperColor's own ([led],
// [extract], [components]…) are kept for the drivers.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "settings/settings.hpp"

namespace wpc {

/// The settings in TOML text. Throws wpc::Error (the message has no file name).
Config parse_settings(std::string_view text);

/// The whole settings as TOML, defaults included (what `wallpapercolor config` prints).
std::string write_settings(const Config& config);

/// $XDG_CONFIG_HOME/wallpapercolor/config.toml (or ~/.config/...); nullopt without HOME.
std::optional<std::filesystem::path> default_settings_path();

/// Reads the file at `path`; a missing file gives the defaults. Throws wpc::Error naming the
/// file.
Config read_settings(const std::filesystem::path& path);

}  // namespace wpc
