// Model: the desktop's wallpaper settings, as the service needs them.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wpc {

struct WallpaperSettings {
    /// Wallpaper plugin, e.g. org.kde.image or org.kde.slideshow.
    std::string plugin;
    /// What is on screen: a picture, or a wallpaper package folder; nullopt when there is none.
    std::optional<std::filesystem::path> image;
    /// Folders of a slideshow (empty for other plugins).
    std::vector<std::filesystem::path> slide_folders;
};

}  // namespace wpc
