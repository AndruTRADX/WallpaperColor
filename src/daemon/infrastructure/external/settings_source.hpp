// Adapter: the shared module's settings_file service, for one file, as the service's
// SettingsSource.

#pragma once

#include "daemon/application/contracts/external/settings_source.hpp"
#include "shared/infrastructure/services/settings_file.hpp"

namespace wpc {

class SettingsFileSource final : public SettingsSource {
public:
    explicit SettingsFileSource(std::filesystem::path path) : path_(std::move(path)) {}
    Config load() override { return file_.read(path_); }
    std::filesystem::path location() override { return path_; }

private:
    SettingsFile file_;
    std::filesystem::path path_;
};

}  // namespace wpc
