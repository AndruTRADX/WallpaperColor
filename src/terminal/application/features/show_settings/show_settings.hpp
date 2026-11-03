// Feature: the settings in use as the file would have them, defaults included
// (`wallpapercolor config`).

#pragma once

#include <string>

#include "shared/domain/settings/settings.hpp"
#include "terminal/application/contracts/external/settings_file.hpp"

namespace wpc {

/// Without trailing blank lines.
std::string show_settings(SettingsFormat& format, const Config& config);

}  // namespace wpc
