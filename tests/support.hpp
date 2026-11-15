// What several test files share.

#pragma once

#include <cstdint>
#include <filesystem>

#include "pictures/picture_files.hpp"

namespace wpc::test {

/// Reads a picture with the decoders' module of this build.
inline Picture read_picture(const std::filesystem::path& path, uint32_t min_side = 0) {
    return PictureFiles(WPC_BUILT_PICTURES).read(path, min_side);
}

}  // namespace wpc::test
