// Turning Plasma's wallpaper setting into the picture on screen: KDE wallpaper packages, and
// whether the colour scheme is dark (a package can have a picture for each).

#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace wpc::kde {

/// `image` itself if it is a picture; for a wallpaper package folder, its biggest picture in
/// contents/images (contents/images_dark first for a dark scheme). nullopt when there is none.
std::optional<std::filesystem::path> package_picture(const std::filesystem::path& image, bool dark);

/// Whether the text of a kdeglobals file says the colour scheme is dark: the window background
/// darker than mid-grey.
bool is_dark_scheme(std::string_view kdeglobals);

/// The same, from the user's kdeglobals; false when it can't be read.
bool colour_scheme_is_dark();

}  // namespace wpc::kde
