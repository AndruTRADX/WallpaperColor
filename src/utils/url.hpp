// file:// URLs as paths. Pure code, nothing about WallpaperColor.

#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace wpc {

/// file:///a/b%20c.png → /a/b c.png; plain absolute paths pass through; anything else: nullopt.
std::optional<std::filesystem::path> url_to_path(std::string_view url);

}  // namespace wpc
