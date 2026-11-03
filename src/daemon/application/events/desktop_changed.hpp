// Event: the wallpaper on screen may have changed. Heard from:
// - the desktop's own signals (wallpaper settings changed, the desktop restarted);
// - its settings files being written (the wallpaper chosen in the settings, a light/dark switch);
// - a picture opened in a slideshow folder: Plasma sends no signal when the slide changes, but
//   plasmashell opens the next picture. Opening the picture on screen is ignored: it is the
//   service reading it, and noticing it would trigger another read.
// Only a hint: the picture always comes from the desktop when the feature runs.

#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

#include "daemon/application/contracts/desktop_signals.hpp"
#include "daemon/application/contracts/file_watch.hpp"
#include "daemon/application/models/notifications.hpp"

namespace wpc {

class DesktopChangedEvent {
public:
    DesktopChangedEvent(DesktopSignals& signals, FileWatch& files, Notify notify)
        : signals_(signals), files_(files), notify_(std::move(notify)) {}

    /// Starts listening to the desktop's signals and settings files.
    void start();

    /// Listens to the slideshow folders of the wallpaper now on screen (none: stops), and
    /// remembers which picture is on screen.
    void follow(const std::vector<std::filesystem::path>& slide_folders,
                const std::optional<std::filesystem::path>& on_screen);

private:
    void slide_changed(const FileChange& change);

    DesktopSignals& signals_;
    FileWatch& files_;
    Notify notify_;
    std::vector<std::filesystem::path> slide_folders_;
    std::vector<int> slide_watches_;
    std::mutex mutex_;  // on_screen_ is read on the file watch's thread
    std::optional<std::filesystem::path> on_screen_;
};

}  // namespace wpc
