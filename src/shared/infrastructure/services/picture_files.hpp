// Service: reading picture files: JPEG, PNG, WebP, GIF and BMP, at full size or smaller (how:
// utils/picture_decoding.hpp).
//
// The decoders and the image libraries are a module, wpc-pictures.so, loaded only while a
// picture is read: the rest of the time they take no memory.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include "shared/domain/colours/picture.hpp"

namespace wpc {

class PictureFiles {
public:
    /// Reads the picture at `path`. `min_side` > 0 allows a smaller picture: both sides shrunk
    /// by the same factor, as far as possible while staying at least `min_side` pixels.
    /// 0 = full size. Throws wpc::Error.
    Picture read(const std::filesystem::path& path, uint32_t min_side = 0) const;

    /// When the file was last changed (a number that grows with time); nullopt if it can't be
    /// read.
    std::optional<int64_t> modified_time(const std::filesystem::path& path) const;
};

}  // namespace wpc
