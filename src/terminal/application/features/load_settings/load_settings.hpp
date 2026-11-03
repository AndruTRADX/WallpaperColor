// Feature: the settings the terminal works with: from the file chosen on the command line, or
// the default one.

#pragma once

#include <filesystem>
#include <optional>

#include "shared/domain/settings/settings.hpp"
#include "terminal/application/contracts/external/settings_file.hpp"
#include "terminal/application/contracts/settings_editor.hpp"

namespace wpc {

struct LoadedSettings {
    /// nullopt when there is no file to use (no HOME).
    std::optional<std::filesystem::path> path;
    Config config;
};

/// Throws wpc::Error for a mistake in the file.
LoadedSettings load_settings(SettingsFormat& format, const std::optional<std::filesystem::path>& chosen);

/// The settings the editor's text gives, as the service will read them (the defaults when it
/// has a mistake).
Config edited_settings(SettingsFormat& format, const SettingsEditor& editor);

}  // namespace wpc
