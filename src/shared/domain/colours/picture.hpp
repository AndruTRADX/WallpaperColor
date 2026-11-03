// A picture as pixels, and shrinking it for colour extraction. Pure maths: reading files is
// the picture_files service's job.
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
    /// Largest channel value: 255 or 65535.
    float max() const { return is_16_bit() ? 65535.0f : 255.0f; }
    /// Channel value number `i` (0 .. width × height × 3).
    float sample(size_t i) const { return is_16_bit() ? rgb16[i] : rgb8[i]; }
};

/// How much a `width` × `height` picture may be shrunk so that both sides stay at least
/// `min_side`: the largest whole number that works, at least 1.
uint32_t shrink_factor(uint32_t width, uint32_t height, uint32_t min_side);

/// Resizes to exactly `width` × `height` with a triangle (linear) filter, then converts to
/// 8 bits per channel.
std::vector<std::array<uint8_t, 3>> resize_to_rgb8(const Picture& image, uint32_t width,
                                                   uint32_t height);

}  // namespace wpc
