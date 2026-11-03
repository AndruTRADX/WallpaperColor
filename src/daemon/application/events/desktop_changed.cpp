#include "daemon/application/events/desktop_changed.hpp"

#include "daemon/domain/picture_names.hpp"
#include "utils/error.hpp"
#include "utils/log.hpp"

namespace wpc {

namespace fs = std::filesystem;

void DesktopChangedEvent::start() {
    signals_.listen([this] { notify_(DesktopChangedNotice{}); });
    for (const fs::path& file : signals_.settings_files()) {
        try {
            files_.watch(file.parent_path(), FILE_WRITTEN | FILE_CREATED, [this, name = file.filename().string()](const FileChange& c) {
                if (c.name == name) notify_(DesktopChangedNotice{});
            });
        } catch (const Error& e) {
            log("not watching {}: {}", file.parent_path().string(), e.what());
        }
    }
}

void DesktopChangedEvent::follow(const std::vector<fs::path>& slide_folders, const std::optional<fs::path>& on_screen) {
    {
        std::lock_guard lock(mutex_);
        on_screen_ = on_screen;
    }
    if (slide_folders == slide_folders_) return;
    for (int id : slide_watches_) files_.unwatch(id);
    slide_watches_.clear();
    slide_folders_ = slide_folders;
    for (const fs::path& folder : slide_folders) {
        try {
            slide_watches_.push_back(
                files_.watch_tree(folder, FILE_OPENED | FILE_CREATED, [this](const FileChange& c) { slide_changed(c); }));
        } catch (const Error& e) {
            log("not watching {}: {}", folder.string(), e.what());
        }
    }
    if (!slide_watches_.empty()) log("watching {} slideshow folder(s)", slide_watches_.size());
}

void DesktopChangedEvent::slide_changed(const FileChange& c) {
    if (c.opened) {
        std::lock_guard lock(mutex_);
        if (on_screen_ == c.folder / c.name) return;  // the service reading the picture on screen
    }
    bool new_folder = c.created && c.is_folder;
    if (is_picture_name(c.name) || new_folder) notify_(DesktopChangedNotice{});
}

}  // namespace wpc
