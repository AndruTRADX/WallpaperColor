// Service: KDE wallpaper packages. A package is a folder with pictures in contents/images (and
// contents/images_dark for a dark colour scheme); its picture is the biggest one there.

#pragma once

#include "daemon/application/contracts/wallpaper_packages.hpp"

namespace wpc {

class PlasmaPackages final : public WallpaperPackages {
public:
    std::optional<std::filesystem::path> picture(const std::filesystem::path& image, bool dark) override;
};

}  // namespace wpc
