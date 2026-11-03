// What the service needs from the desktop: its wallpaper settings now.

#pragma once

#include "daemon/application/models/wallpaper_settings.hpp"

namespace wpc {

class DesktopWallpaper {
public:
    virtual ~DesktopWallpaper() = default;
    /// The wallpaper settings of the first screen. Throws wpc::Error.
    virtual WallpaperSettings current() = 0;
};

}  // namespace wpc
