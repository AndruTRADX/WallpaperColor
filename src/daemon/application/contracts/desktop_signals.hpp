// What the service needs to hear when the desktop's wallpaper may have changed.

#pragma once

#include <filesystem>
#include <functional>
#include <vector>

namespace wpc {

class DesktopSignals {
public:
    virtual ~DesktopSignals() = default;
    /// Calls `changed` (on the service's loop) on the desktop's own signals (wallpaper settings
    /// changed, the desktop restarted). Throws wpc::Error.
    virtual void listen(std::function<void()> changed) = 0;
    /// The desktop's files whose writing may change the wallpaper (settings, colour scheme).
    virtual std::vector<std::filesystem::path> settings_files() = 0;
};

}  // namespace wpc
