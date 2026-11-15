// The SettingsSource port: the settings file at one path (settings/settings_file.hpp reads it).

#pragma once

#include <filesystem>

#include "daemon/service/ports.hpp"
#include "settings/settings_file.hpp"

namespace wpc {

class SettingsFileSource final : public SettingsSource {
public:
    explicit SettingsFileSource(std::filesystem::path path) : path_(std::move(path)) {}
    Config load() override { return read_settings(path_); }
    std::filesystem::path location() override { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace wpc
