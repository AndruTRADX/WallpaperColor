// What makes the service act: each event listens through the ports for something happening and
// sends a notification that it happened. It doesn't run use cases or decide what happens next:
// the service loop receives the notification and does that. A notification is only a trigger:
// the answer always comes from the desktop when the loop acts, so a false alarm costs one
// question to the desktop and changes nothing.
//
//   DesktopChangedEvent    the wallpaper on screen may have changed
//   SettingsChangedEvent   the settings may have changed
//   HardwareChangedEvent   the hardware may have lost or regained its colours
//   RequestReceivedEvent   the terminal sent a request

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "daemon/service/ports.hpp"
#include "utils/channel.hpp"
#include "utils/event_queue.hpp"

namespace wpc {

// ---------------------------------------------------------------------------------------------
// Notifications

/// The wallpaper on screen may have changed.
struct DesktopChangedNotice {};
/// The settings may have changed.
struct SettingsChangedNotice {};
/// The hardware may have lost or regained its colours: paint again, even the same wallpaper.
struct HardwareChangedNotice {
    /// The computer woke from sleep: what the devices show is not known any more.
    bool woke = false;
};

/// Where a request's answer goes: from the loop's thread to the request server's thread.
class Reply {
public:
    void send(std::string line) { answer_.send(std::move(line)); }
    /// The answer, waited for at most `limit`; nullopt when it didn't come in time: the request
    /// is then given up, and the loop doesn't run it any more (the terminal was told the
    /// service is busy, so it must not happen anyway).
    std::optional<std::string> wait(std::chrono::steady_clock::duration limit) {
        std::optional<std::string> answer = answer_.receive_for(limit);
        if (!answer) given_up_ = true;
        return answer;
    }
    bool given_up() const { return given_up_; }

private:
    Channel<std::string> answer_;
    std::atomic<bool> given_up_ = false;
};

/// The terminal sent a request line; the answer line goes to `reply`.
struct RequestNotice {
    std::string line;
    std::shared_ptr<Reply> reply;
};

using Notification = std::variant<DesktopChangedNotice, SettingsChangedNotice, HardwareChangedNotice, RequestNotice>;
using Notify = std::function<void(Notification)>;
/// Where the notifications wait for the service loop, which sleeps in the events' EventLoop.
using Notifications = EventQueue<Notification>;

// ---------------------------------------------------------------------------------------------
// Events

/// The wallpaper on screen may have changed. Heard from:
/// - the desktop's own signals (wallpaper settings changed, the desktop restarted);
/// - its settings files being written (the wallpaper chosen in the settings, a light/dark switch);
/// - a picture opened in a slideshow folder: Plasma sends no signal when the slide changes, but
///   plasmashell opens the next picture. Opening the picture on screen is ignored: it is the
///   service reading it, and noticing it would trigger another read.
/// The slideshow folders are watched with every folder below them. A folder created or removed
/// in there, the folder itself removed, or changes lost, read the folders again at the next
/// follow().
class DesktopChangedEvent {
public:
    DesktopChangedEvent(Desktop& desktop, FileWatch& files, Notify notify)
        : desktop_(desktop), files_(files), notify_(std::move(notify)) {}

    /// Starts listening to the desktop's signals and settings files.
    void start();

    /// The picture on screen, about to be read: its opening is not a change.
    void reading(const std::filesystem::path& picture);

    /// Listens to the slideshow folders of the wallpaper now on screen (none: stops). Reads
    /// the folders again when they or what is in them changed. Returns what could not be
    /// watched (try again later), or nullopt.
    std::optional<std::string> follow(const std::vector<std::filesystem::path>& slide_folders);

private:
    void slide_changed(const FileChange& change);

    Desktop& desktop_;
    FileWatch& files_;
    Notify notify_;
    std::vector<std::filesystem::path> slide_folders_;
    std::vector<int> slide_watches_;
    /// Every folder of slide_folders_ is watched.
    bool following_all_ = true;
    /// The folders changed: read them again.
    bool reread_ = false;
    std::optional<std::filesystem::path> on_screen_;
};

/// The settings may have changed (their file was written; its folder was created).
class SettingsChangedEvent {
public:
    SettingsChangedEvent(SettingsSource& settings, FileWatch& files, Notify notify)
        : settings_(settings), files_(files), notify_(std::move(notify)) {}

    /// Starts listening. Throws wpc::Error when the folder can't be watched.
    void start();

private:
    void watch_folder();

    SettingsSource& settings_;
    FileWatch& files_;
    Notify notify_;
    bool watching_folder_ = false;
};

/// The hardware may have lost or regained its colours, with no new wallpaper. Heard from:
/// - the computer waking from sleep (devices may come back with their own colours);
/// - a device's file added (something plugged in again: a keyboard, a USB controller).
/// The service paints again, so a device that comes back doesn't wait for the next wallpaper.
class HardwareChangedEvent {
public:
    HardwareChangedEvent(SystemEvents& system, FileWatch& files, Notify notify)
        : system_(system), files_(files), notify_(std::move(notify)) {}

    /// Starts listening. Throws wpc::Error when the device folder can't be watched.
    void start();

private:
    SystemEvents& system_;
    FileWatch& files_;
    Notify notify_;
};

/// The terminal sent a request. The notification carries the line and where the answer goes;
/// if the service is busy (a fade) for longer than `patience`, `busy_answer` is sent instead
/// and the request is given up.
class RequestReceivedEvent {
public:
    static constexpr std::chrono::seconds PATIENCE{2};

    RequestReceivedEvent(RequestServer& server, Notify notify, std::string busy_answer)
        : server_(server), notify_(std::move(notify)), busy_answer_(std::move(busy_answer)) {}
    /// Stops the server first: its thread must not call this event while it goes.
    ~RequestReceivedEvent() { server_.stop(); }
    RequestReceivedEvent(const RequestReceivedEvent&) = delete;
    RequestReceivedEvent& operator=(const RequestReceivedEvent&) = delete;

    /// Starts listening. Throws wpc::Error.
    void start();

private:
    RequestServer& server_;
    Notify notify_;
    std::string busy_answer_;
};

}  // namespace wpc
