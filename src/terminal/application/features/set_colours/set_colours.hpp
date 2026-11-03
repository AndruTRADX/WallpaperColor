// Feature: paint every chosen component once, with given colours or a picture's
// (`wallpapercolor set`). The service paints over it at the next wallpaper change.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "shared/domain/settings/settings.hpp"
#include "terminal/application/contracts/external/driver_modules.hpp"
#include "terminal/application/contracts/external/pictures.hpp"

namespace wpc {

struct ColoursToSet {
    /// One to three colours ("#rrggbb"), or…
    std::vector<std::string> colours;
    /// …a picture to take them from.
    std::optional<std::filesystem::path> picture;
    /// How to spread them; nullopt: as the settings say.
    std::optional<Mode> mode;
};

struct SetResult {
    std::vector<Rgb> stops;
    /// Every device painted.
    std::vector<std::string> painted;
    /// What wasn't painted, and why ("Strip: unplugged").
    std::vector<std::string> not_painted;
};

/// Throws wpc::Error (a bad colour, an unreadable picture, nothing painted).
SetResult set_colours(DriverModules& drivers, Pictures& pictures, const Config& config, const ColoursToSet& what);

}  // namespace wpc
