// Which file names are pictures a wallpaper can be (by extension, any case).

#pragma once

#include <string_view>

namespace wpc {

bool is_picture_name(std::string_view name);

}  // namespace wpc
