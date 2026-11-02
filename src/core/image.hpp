// Decoded pictures and the resize used before colour extraction.
//
// Pixels are stored as floats holding whole numbers (0-255, or 0-65535 for 16-bit PNGs),
// because the resize works in floats anyway. Only red, green and blue are kept: alpha is
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
    /// Largest channel value: 255 (8-bit) or 65535 (16-bit).
    float max = 255;
    /// width × height × 3 values, row by row, red green blue.
    std::vector<float> rgb;
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
