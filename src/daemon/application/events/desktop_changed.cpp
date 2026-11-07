#include "daemon/application/events/desktop_changed.hpp"

#include <format>

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
                if (c.name == name || c.overflow) notify_(DesktopChangedNotice{});
            });
        } catch (const Error& e) {
            log("not watching {}: {}", file.parent_path().string(), e.what());
        }
    }
}

void DesktopChangedEvent::showing(const std::optional<fs::path>& picture) {
    on_screen_ = picture;
}

std::optional<std::string> DesktopChangedEvent::follow(const std::vector<fs::path>& slide_folders) {
    bool new_folders = slide_folders != slide_folders_;
    if (!new_folders && following_all_ && !reread_) return std::nullopt;
    reread_ = false;

    // The new watches before the old ones go: no change is missed in between.
    std::vector<int> old = std::move(slide_watches_);
    slide_watches_.clear();
    slide_folders_ = slide_folders;
    std::optional<std::string> problem;
    for (const fs::path& folder : slide_folders) {
        try {
            slide_watches_.push_back(files_.watch_tree(folder, FILE_OPENED | FILE_CREATED | FILE_REMOVED,
                                                       [this](const FileChange& c) { slide_changed(c); }));
        } catch (const Error& e) {
            if (!problem) problem = std::format("not watching {}: {}", folder.string(), e.what());
        }
    }
    for (int id : old) files_.unwatch(id);
    following_all_ = !problem;
    if (new_folders && !slide_watches_.empty()) log("watching {} slideshow folder(s)", slide_watches_.size());
    return problem;
}

void DesktopChangedEvent::slide_changed(const FileChange& c) {
    bool tree_changed = c.overflow || (c.is_folder && (c.created || c.removed));
    if (tree_changed) {
        reread_ = true;
        return notify_(DesktopChangedNotice{});
    }
    if (c.opened && on_screen_ == c.folder / c.name) return;  // the service reading the picture on screen
    if (is_picture_name(c.name)) notify_(DesktopChangedNotice{});
}

}  // namespace wpc
