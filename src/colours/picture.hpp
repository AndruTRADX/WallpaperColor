// A picture as pixels, and shrinking it for colour extraction. Pure maths: reading files is
// pictures/ (the decoders' module).
//
// Pixels are kept as decoded: one byte per channel, or two for 16-bit PNGs. Only red, green
// and blue are kept: alpha is dropped and grey is copied to all three channels.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace wpc {

struct Picture {
    uint32_t width = 0;
    uint32_t height = 0;
    /// width × height × 3 values, row by row, red green blue. One of the two is filled:
    /// `rgb8` for 8-bit pictures (almost all), `rgb16` for 16-bit PNGs.
    std::vector<uint8_t> rgb8;
    std::vector<uint16_t> rgb16;

    bool is_16_bit() const { return !rgb16.empty(); }
};

/// Resizes to exactly `width` × `height` with a triangle (linear) filter, then converts to
/// 8 bits per channel. A picture whose buffer doesn't match its size is taken as black.
std::vector<std::array<uint8_t, 3>> resize_to_rgb8(const Picture& image, uint32_t width, uint32_t height);

}  // namespace wpc
