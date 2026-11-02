// KDE Plasma wallpaper source.
//
// org.kde.PlasmaShell.wallpaper(screen) returns the wallpaper settings of a screen, and its
// `Image` key is the picture on screen right now, including the current slide of a slideshow.
// Plasma signals `wallpaperChanged` only when the settings change (not on every slide), so
// slide changes must be detected some other way (the service watches the slideshow folders).

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sdbus {
class IConnection;
class IProxy;
}  // namespace sdbus

namespace wpc::kde {

struct Wallpaper {
    /// Wallpaper plugin, e.g. org.kde.image or org.kde.slideshow.
    std::string plugin;
    /// Picture on screen: a file, or a wallpaper package folder (see resolve_image()).
    std::optional<std::filesystem::path> image;
    /// Folders of a slideshow (empty for other plugins).
    std::vector<std::filesystem::path> slide_paths;

    bool is_slideshow() const { return plugin == "org.kde.slideshow"; }
};

class Plasma {
public:
    /// Connects to the session bus. Throws wpc::Error.
    static Plasma connect();
    ~Plasma();
    Plasma(Plasma&&) noexcept;
    Plasma& operator=(Plasma&&) noexcept;

    /// Wallpaper of `screen` (0 = first screen). Throws wpc::Error.
    Wallpaper wallpaper(uint32_t screen);

    /// Calls `callback` (from the D-Bus thread) on every `wallpaperChanged` signal (settings
    /// changed, plugin switched…) and whenever plasmashell starts or stops. Throws wpc::Error.
    void listen(std::function<void()> callback);

private:
    Plasma() = default;
    std::unique_ptr<sdbus::IConnection> connection_;
    std::unique_ptr<sdbus::IProxy> proxy_;
};

/// file:///a/b%20c.png → /a/b c.png; plain absolute paths pass through; anything else: nullopt.
std::optional<std::filesystem::path> url_to_path(std::string_view url);

/// The picture to analyse: `path` itself if it is a file, or for a wallpaper package folder its
/// biggest picture in contents/images (contents/images_dark first when `dark`).
std::optional<std::filesystem::path> resolve_image(const std::filesystem::path& path, bool dark);

/// ~/.config/kdeglobals
std::optional<std::filesystem::path> kdeglobals_path();

/// Whether the colour scheme is dark: the window background is darker than mid-grey.
bool dark_mode();

/// The same test on the text of a kdeglobals file.
bool is_dark_scheme(std::string_view kdeglobals);

}  // namespace wpc::kde
