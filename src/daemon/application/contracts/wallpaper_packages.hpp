// What the service needs to turn a wallpaper into one picture.

#pragma once

#include <filesystem>
#include <optional>

namespace wpc {

class WallpaperPackages {
public:
    virtual ~WallpaperPackages() = default;
    /// `image` itself if it is a picture; for a wallpaper package folder, its picture for a
    /// dark or light colour scheme. nullopt when there is none.
    virtual std::optional<std::filesystem::path> picture(const std::filesystem::path& image, bool dark) = 0;
};

}  // namespace wpc
