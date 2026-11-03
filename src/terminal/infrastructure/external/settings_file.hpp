// Adapter: the shared module's settings_file service as the terminal's SettingsFormat.

#pragma once

#include "shared/infrastructure/services/settings_file.hpp"
#include "terminal/application/contracts/external/settings_file.hpp"

namespace wpc {

class TomlSettingsFormat final : public SettingsFormat {
public:
    std::optional<std::filesystem::path> default_path() override { return file_.default_path(); }
    Config read(const std::filesystem::path& path) override { return file_.read(path); }
    Config parse(std::string_view text) override { return file_.parse(text); }
    std::string write(const Config& config) override { return file_.write(config); }

private:
    SettingsFile file_;
};

}  // namespace wpc
