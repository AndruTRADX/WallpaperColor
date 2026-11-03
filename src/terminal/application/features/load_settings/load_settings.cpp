#include "terminal/application/features/load_settings/load_settings.hpp"

#include "utils/error.hpp"

namespace wpc {

LoadedSettings load_settings(SettingsFormat& format, const std::optional<std::filesystem::path>& chosen) {
    LoadedSettings out{chosen ? chosen : format.default_path(), Config{}};
    if (out.path) out.config = format.read(*out.path);
    return out;
}

Config edited_settings(SettingsFormat& format, const SettingsEditor& editor) {
    try {
        return format.parse(editor.text());
    } catch (const Error&) {
        return Config{};
    }
}

}  // namespace wpc
