// Service: KDE Plasma over D-Bus. The wallpaper settings of the first screen
// (org.kde.PlasmaShell.wallpaper: its `Image` is the picture on screen right now, including the
// current slide of a slideshow), Plasma's signals, and the files Plasma writes its settings to.
// Plasma signals `wallpaperChanged` only when the settings change, not on every slide.

#pragma once

#include <memory>

#include "daemon/application/contracts/desktop_signals.hpp"
#include "daemon/application/contracts/desktop_wallpaper.hpp"

namespace sdbus {
class IConnection;
class IProxy;
}  // namespace sdbus

namespace wpc {

class PlasmaShell final : public DesktopWallpaper, public DesktopSignals {
public:
    /// Connects to the session bus. Throws wpc::Error.
    PlasmaShell();
    ~PlasmaShell() override;

    WallpaperSettings current() override;
    /// Plasma's `wallpaperChanged`, and plasmashell starting or stopping.
    void listen(std::function<void()> changed) override;
    /// plasma-org.kde.plasma.desktop-appletsrc (the wallpaper chosen in the settings, which
    /// doesn't always send a signal) and kdeglobals (a light/dark switch can pick another
    /// picture of a wallpaper package).
    std::vector<std::filesystem::path> settings_files() override;

private:
    std::unique_ptr<sdbus::IConnection> connection_;
    std::unique_ptr<sdbus::IProxy> proxy_;
};

}  // namespace wpc
