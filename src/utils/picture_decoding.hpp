// Decoding picture files: JPEG (libjpeg-turbo), PNG (libpng), WebP (libwebp), GIF and BMP
// (stb_image), to red, green, blue. The format comes from the file's content, not its extension
// (wallpapers are often PNGs named `.jpg`).
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
//
// The decoders are not linked into the programs: picture_decoding.cpp is its own module,
// wpc-pictures.so, which a program loads only while it reads a picture (with the image
// libraries it needs), so they take no memory the rest of the time. Like a driver module, it
// must come from the programs' build: `built_for` is checked.

#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace wpc {

struct DecodedPicture {
    uint32_t width = 0;
    uint32_t height = 0;
    /// width × height × 3 values, row by row, red green blue: `rgb8` for 8-bit pictures,
    /// `rgb16` for 16-bit PNGs. Alpha is dropped and grey is copied to all three channels.
    std::vector<uint8_t> rgb8;
    std::vector<uint16_t> rgb16;
};

/// What wpc-pictures.so offers.
struct PictureDecoder {
    /// WPC_BUILD_ID of the build it comes from.
    const char* built_for;
    /// Reads the picture at `path`. `min_side` > 0 allows a smaller picture: both sides shrunk
    /// by the same factor (shrink_factor), as far as possible while staying at least
    /// `min_side` pixels. 0 = full size. Throws wpc::Error.
    DecodedPicture (*read)(const std::filesystem::path& path, uint32_t min_side);
};

/// How much a `width` × `height` picture may be shrunk so that both sides stay at least
/// `min_side`: the largest whole number that works, at least 1.
inline uint32_t shrink_factor(uint32_t width, uint32_t height, uint32_t min_side) {
    uint32_t smaller = std::min(width, height);
    if (min_side == 0 || smaller <= min_side) return 1;
    return smaller / min_side;
}

}  // namespace wpc

/// The module's only exported symbol.
extern "C" [[gnu::visibility("default")]] const wpc::PictureDecoder* wpc_picture_decoder();
