#include "daemon/application/events/settings_changed.hpp"

#include "utils/error.hpp"

namespace wpc {

void SettingsChangedEvent::start() {
    std::filesystem::path folder = settings_.location().parent_path();
    // If the folder doesn't exist yet, watching it fails: wait for it to be created.
    try {
        return watch_folder();
    } catch (const Error&) {
    }
    files_.watch(folder.parent_path(), FILE_CREATED, [this, name = folder.filename().string()](const FileChange& c) {
        if (c.name != name || !c.is_folder) return;
        watch_folder();
        notify_(SettingsChangedNotice{});
    });
}

void SettingsChangedEvent::watch_folder() {
    if (watching_folder_) return;
    std::filesystem::path file = settings_.location();
    files_.watch(file.parent_path(), FILE_WRITTEN | FILE_CREATED, [this, name = file.filename().string()](const FileChange& c) {
        if (c.name == name || c.overflow) notify_(SettingsChangedNotice{});
    });
    watching_folder_ = true;
}

}  // namespace wpc
