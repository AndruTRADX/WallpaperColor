#include "daemon/service/events.hpp"

#include <format>

#include "daemon/service/rules.hpp"
#include "utils/error.hpp"
#include "utils/log.hpp"

namespace wpc {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------
// The desktop

void DesktopChangedEvent::start() {
    desktop_.listen([this] { notify_(DesktopChangedNotice{}); });
    for (const fs::path& file : desktop_.settings_files()) {
        try {
            files_.watch(file.parent_path(), FILE_WRITTEN | FILE_CREATED, [this, name = file.filename().string()](const FileChange& c) {
                if (c.name == name || c.overflow) notify_(DesktopChangedNotice{});
            });
        } catch (const Error& e) {
            log("not watching {}: {}", file.parent_path().string(), e.what());
        }
    }
}

void DesktopChangedEvent::reading(const fs::path& picture) {
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

// ---------------------------------------------------------------------------------------------
// The settings

void SettingsChangedEvent::start() {
    fs::path folder = settings_.location().parent_path();
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
    fs::path file = settings_.location();
    files_.watch(file.parent_path(), FILE_WRITTEN | FILE_CREATED, [this, name = file.filename().string()](const FileChange& c) {
        if (c.name == name || c.overflow) notify_(SettingsChangedNotice{});
    });
    watching_folder_ = true;
}

// ---------------------------------------------------------------------------------------------
// The hardware

void HardwareChangedEvent::start() {
    try {
        system_.listen([this] { notify_(HardwareChangedNotice{.woke = true}); });
    } catch (const Error& e) {
        log("not hearing when the computer wakes up: {}", e.what());
    }
    // Any device: which ones a driver paints is the drivers' business; a false alarm costs a
    // repaint with the same colours.
    files_.watch(system_.device_folder(), FILE_CREATED, [this](const FileChange& c) {
        if (c.created || c.overflow) notify_(HardwareChangedNotice{.woke = false});
    });
}

// ---------------------------------------------------------------------------------------------
// The terminal

void RequestReceivedEvent::start() {
    server_.serve([this](const std::string& line) {
        auto reply = std::make_shared<Reply>();
        notify_(RequestNotice{line, reply});
        return reply->wait(PATIENCE).value_or(busy_answer_);
    });
}

}  // namespace wpc
