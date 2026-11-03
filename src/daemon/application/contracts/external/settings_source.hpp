// What the service needs from the settings (the shared module's settings_file service).

#pragma once

#include <filesystem>

#include "shared/domain/settings/settings.hpp"

namespace wpc {

class SettingsSource {
public:
    virtual ~SettingsSource() = default;
    /// The settings; the defaults when there are none yet. Throws wpc::Error for a mistake.
    virtual Config load() = 0;
    /// The file they are in.
    virtual std::filesystem::path location() = 0;
};

}  // namespace wpc
