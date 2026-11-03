// Feature: save the edited settings. The service notices the file change and applies it.

#pragma once

#include <optional>
#include <string>

#include "shared/domain/settings/settings.hpp"
#include "terminal/application/contracts/external/settings_file.hpp"
#include "terminal/application/contracts/settings_editor.hpp"

namespace wpc {

struct SavedSettings {
    /// The settings now (also when saving failed: they are what the editor has).
    Config config;
    /// Why the file couldn't be written.
    std::optional<std::string> error;
    /// The settings colours are taken with changed: a preview of them must be taken again.
    bool extract_changed = false;
};

/// `before`: the settings before the edit.
SavedSettings save_settings(SettingsEditor& editor, SettingsFormat& format, const Config& before);

}  // namespace wpc
