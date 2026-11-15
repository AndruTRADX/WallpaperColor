// The service's ports: what it needs from the world outside, as interfaces. Everything in
// service/ talks to the world only through these (and through DriverHost and PictureReader,
// the ports both programs share), so the tests run it with fakes: no desktop, no files, no
// D-Bus, no hardware, no clock. The real ones are in kde/ and system/, wired in main.cpp.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "settings/settings.hpp"

namespace wpc {

// ---------------------------------------------------------------------------------------------
// The desktop

/// What the desktop shows now.
struct OnScreen {
    /// The wallpaper plugin (org.kde.image, org.kde.slideshow…); empty when the desktop has no
    /// screen for now (the monitor went off).
    std::string plugin;
    /// The picture file on screen, chosen for the colour scheme when the wallpaper offers
    /// light and dark ones; nullopt when there is none.
    std::optional<std::filesystem::path> picture;
    /// Folders of a slideshow (empty for other plugins): their pictures may come on screen
    /// with no signal saying so.
    std::vector<std::filesystem::path> slide_folders;
};

/// The desktop environment (KDE Plasma: kde/plasma.hpp). Another desktop is another
/// implementation of this, picked in main.cpp.
class Desktop {
public:
    virtual ~Desktop() = default;
    /// What is on screen now. Throws wpc::Error when the desktop can't be asked.
    virtual OnScreen on_screen() = 0;
    /// Calls `changed` (on the service's loop) on the desktop's own signals (wallpaper settings
    /// changed, the desktop restarted). Throws wpc::Error.
    virtual void listen(std::function<void()> changed) = 0;
    /// The desktop's files whose writing may change what is on screen (its settings, the colour
    /// scheme).
    virtual std::vector<std::filesystem::path> settings_files() = 0;
};

// ---------------------------------------------------------------------------------------------
// Files

/// What to hear about.
enum FileChanges : uint32_t {
    FILE_OPENED = 1,
    FILE_WRITTEN = 2,  // a file finished being written, or moved in
    FILE_CREATED = 4,  // created, or moved in
    FILE_REMOVED = 8,  // removed or moved out; also the watched folder itself
};

/// A change to a file in a watched folder.
struct FileChange {
    /// The folder it happened in.
    std::filesystem::path folder;
    /// File name inside the folder.
    std::string name;
    bool opened = false;
    bool created = false;
    /// Removed or moved out. With an empty name: the watched folder itself, whose watch has
    /// ended (watch it again once it is back).
    bool removed = false;
    /// The name is a folder.
    bool is_folder = false;
    /// Changes were lost (the kernel's queue overflowed): anything in `folder` may have
    /// changed. The name is empty.
    bool overflow = false;
};

/// Hearing about files changing in folders (inotify: system/file_watch.hpp).
class FileWatch {
public:
    virtual ~FileWatch() = default;
    /// Calls `changed` (on the service's loop) for every change of kind `what` (FileChanges)
    /// in `folder`. Returns an id for unwatch(). Throws wpc::Error.
    virtual int watch(const std::filesystem::path& folder, uint32_t what,
                      std::function<void(const FileChange&)> changed) = 0;
    /// The same for `folder` and every folder below it, as one watch: all of them or, when one
    /// can't be watched, none (Throws wpc::Error).
    virtual int watch_tree(const std::filesystem::path& folder, uint32_t what,
                           std::function<void(const FileChange&)> changed) = 0;
    virtual void unwatch(int id) = 0;
};

/// The settings file (system/settings_source.hpp reads the real one).
class SettingsSource {
public:
    virtual ~SettingsSource() = default;
    /// The settings; the defaults when there are none yet. Throws wpc::Error for a mistake.
    virtual Config load() = 0;
    /// The file they are in.
    virtual std::filesystem::path location() = 0;
};

// ---------------------------------------------------------------------------------------------
// The computer

using TimePoint = std::chrono::steady_clock::time_point;

/// Time (system/clock.hpp). Faked in the tests, so hours of retries run in no time.
class Clock {
public:
    virtual ~Clock() = default;
    /// Now, on a clock that only goes forward (for waits).
    virtual TimePoint now() = 0;
    /// Returns at `time` (at once if it has passed).
    virtual void wait_until(TimePoint time) = 0;
    /// Seconds since the Unix epoch (for `status`).
    virtual uint64_t unix_seconds() = 0;
};

/// When the hardware may have lost or regained its colours (logind: system/logind.hpp).
class SystemEvents {
public:
    virtual ~SystemEvents() = default;
    /// Calls `woke` (on the service's loop) when the computer wakes from sleep: devices may have
    /// lost their colours. Throws wpc::Error.
    virtual void listen(std::function<void()> woke) = 0;
    /// Where the kernel adds a device's file when one is plugged in.
    virtual std::filesystem::path device_folder() = 0;
};

/// The terminal's requests (the Unix socket: system/request_server.hpp): lines in, lines out.
class RequestServer {
public:
    virtual ~RequestServer() = default;
    /// Listens (on a thread of its own) and answers every request line with `answer`'s line.
    /// Throws wpc::Error when it can't listen.
    virtual void serve(std::function<std::string(const std::string& request)> answer) = 0;
    /// Stops listening: once it returns, `answer` is not called any more (what it uses may go).
    virtual void stop() = 0;
};

}  // namespace wpc
