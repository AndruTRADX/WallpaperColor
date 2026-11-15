// The Desktop port for KDE Plasma, over D-Bus: the wallpaper settings of the first screen
// (org.kde.PlasmaShell.wallpaper: its `Image` is the picture on screen right now, including the
// current slide of a slideshow), Plasma's signals, and the files Plasma writes its settings to.
// Plasma signals `wallpaperChanged` only when the settings change, not on every slide. The
// signals are read in the service's loop (no thread of their own).

#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include "daemon/service/ports.hpp"
#include "utils/event_loop.hpp"

namespace sdbus {
class IConnection;
class IProxy;
}  // namespace sdbus

namespace wpc {

class PlasmaDesktop final : public Desktop {
public:
    /// The longest Plasma may take to answer: the service's loop waits for it, so a Plasma that
    /// hangs must not hang the service (D-Bus's own limit is 25 s).
    static constexpr std::chrono::seconds CALL_TIMEOUT{3};

    /// Connects to the session bus; its signals will be heard in `loop`. Throws wpc::Error.
    explicit PlasmaDesktop(EventLoop& loop);
    ~PlasmaDesktop() override;

    /// The picture of a wallpaper package is the one for the colour scheme (kde/wallpaper.hpp).
    OnScreen on_screen() override;
    /// Plasma's `wallpaperChanged`, and plasmashell starting or stopping.
    void listen(std::function<void()> changed) override;
    /// plasma-org.kde.plasma.desktop-appletsrc (the wallpaper chosen in the settings, which
    /// doesn't always send a signal) and kdeglobals (a light/dark switch can pick another
    /// picture of a wallpaper package).
    std::vector<std::filesystem::path> settings_files() override;

private:
    std::unique_ptr<sdbus::IConnection> connection_;
    std::unique_ptr<sdbus::IProxy> proxy_;
    EventLoop& loop_;
    std::optional<BusFds> bus_fds_;  // while listening
};

}  // namespace wpc
