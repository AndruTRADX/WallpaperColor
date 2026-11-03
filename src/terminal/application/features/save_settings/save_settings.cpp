#include "terminal/application/features/save_settings/save_settings.hpp"

#include "terminal/application/features/load_settings/load_settings.hpp"
#include "utils/error.hpp"

namespace wpc {

SavedSettings save_settings(SettingsEditor& editor, SettingsFormat& format, const Config& before) {
    SavedSettings out{edited_settings(format, editor), std::nullopt, false};
    out.extract_changed = out.config.extract != before.extract;
    try {
        editor.save();
    } catch (const Error& e) {
        out.error = e.what();
    }
    return out;
}

}  // namespace wpc
