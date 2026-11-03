// What the terminal needs from the settings file's format (the shared module's settings_file
// service).

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "shared/domain/settings/settings.hpp"

namespace wpc {

class SettingsFormat {
public:
    virtual ~SettingsFormat() = default;
    /// Where the settings are by default; nullopt without HOME.
    virtual std::optional<std::filesystem::path> default_path() = 0;
    /// Reads the file; a missing file gives the defaults. Throws wpc::Error.
    virtual Config read(const std::filesystem::path& path) = 0;
    /// The settings in a text. Throws wpc::Error.
    virtual Config parse(std::string_view text) = 0;
    /// The whole settings as text, defaults included.
    virtual std::string write(const Config& config) = 0;
};

}  // namespace wpc
