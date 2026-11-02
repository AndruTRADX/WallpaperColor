// Decoded pictures and the resize used before colour extraction.
//
// Pixels are kept as decoded: one byte per channel, or two for 16-bit PNGs. Only red, green
// and blue are kept: alpha is
// dropped and grey is copied to all three channels, which is what the Rust version's
// `to_rgb8()` does at the end (the resize treats every channel on its own, so dropping
// alpha first gives the same pixels).

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace wpc {

struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    /// width × height × 3 values, row by row, red green blue. One of the two is filled:
    /// `rgb8` for 8-bit pictures (almost all), `rgb16` for 16-bit PNGs.
    std::vector<uint8_t> rgb8;
    std::vector<uint16_t> rgb16;

    bool is_16_bit() const { return !rgb16.empty(); }
    /// Largest channel value: 255 or 65535.
    float max() const { return is_16_bit() ? 65535.0f : 255.0f; }
    /// Channel value number `i` (0 .. width × height × 3).
    float sample(size_t i) const { return is_16_bit() ? rgb16[i] : rgb8[i]; }
};

/// Reads a JPEG, PNG, WebP, GIF (first frame) or BMP. The format comes from the file's
/// content, not its extension (wallpapers are often PNGs named `.jpg`). Throws wpc::Error.
Image load_image(const std::filesystem::path& path);

/// Resizes to exactly `width` × `height` with a triangle filter, then converts to 8 bits.
/// A line-by-line port of `DynamicImage::resize_exact(.., FilterType::Triangle)` followed by
/// `to_rgb8()` from the Rust `image` crate 0.25, so the pixels come out identical.
std::vector<std::array<uint8_t, 3>> resize_to_rgb8(const Image& image, uint32_t width,
                                                   uint32_t height);

}  // namespace wpc
