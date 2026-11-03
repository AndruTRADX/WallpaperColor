// Service: reading picture files: JPEG (libjpeg-turbo), PNG (libpng), WebP (libwebp), GIF and BMP
// (stb_image). The format comes from the file's content, not its extension (wallpapers are
// often PNGs named `.jpg`).
//
// The colour picker looks at about 100×100 pixels, and a 4K wallpaper has 800 times more. So a
// smaller picture can be asked for, and then the full-size one is never built:
// - JPEG: libjpeg-turbo decodes straight to 1/2, 1/4 or 1/8 of the size, skipping most of
//   the work;
// - PNG: libpng hands over one row at a time, and blocks of pixels are averaged as the rows
//   arrive (interlaced PNGs, which are rare, still need the whole picture first);
// - WebP: libwebp decodes straight to the smaller size;
// - GIF and BMP: full size.
// The compressed file is read as the decoder needs it, not loaded first (WebP, GIF and BMP
// excepted: those files are small).

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
