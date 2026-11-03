#include "daemon/domain/picture_names.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>

namespace wpc {

namespace {
constexpr std::array<std::string_view, 6> PICTURE_EXTENSIONS = {"png", "jpg", "jpeg", "webp", "gif", "bmp"};
}  // namespace

bool is_picture_name(std::string_view name) {
    std::string ext = std::filesystem::path(name).extension().string();
    if (!ext.empty()) ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return std::find(PICTURE_EXTENSIONS.begin(), PICTURE_EXTENSIONS.end(), ext) != PICTURE_EXTENSIONS.end();
}

}  // namespace wpc
