#include "colours/picture.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace wpc {

namespace {

// ---------------------------------------------------------------------------------------------
// Resize: a separable triangle filter. First every column is resampled to the new height
// (kept as unrounded floats), then every row to the new width. Each output pixel is the
// weighted average of the source pixels under a triangle centred on it, as wide as one output
// pixel on each side when shrinking.

float triangle_kernel(float x) {
    return (std::abs(x) < 1.0f) ? 1.0f - std::abs(x) : 0.0f;
}

/// Weights of the source pixels that make output pixel `out` along one axis, and the first of
/// those source pixels. `in_size` source pixels become `out_size`.
struct Taps {
    uint32_t first = 0;
    std::vector<float> weights;
};

Taps taps_for(uint32_t out, uint32_t in_size, uint32_t out_size) {
    const float support = 1.0f;  // triangle filter
    float ratio = static_cast<float>(in_size) / static_cast<float>(out_size);
    float sratio = (ratio < 1.0f) ? 1.0f : ratio;
    float src_support = support * sratio;

    // The point in the input that is the centre of this output pixel.
    float input = (static_cast<float>(out) + 0.5f) * ratio;

    // Source pixels i with left <= i < right take part.
    int64_t left = static_cast<int64_t>(std::floor(input - src_support));
    left = std::clamp<int64_t>(left, 0, static_cast<int64_t>(in_size) - 1);
    int64_t right = static_cast<int64_t>(std::ceil(input + src_support));
    right = std::clamp<int64_t>(right, left + 1, static_cast<int64_t>(in_size));

    // Back to the left edge of the pixel: the kernel treats a pixel's centre as 0.
    input = input - 0.5f;

    Taps taps;
    taps.first = static_cast<uint32_t>(left);
    taps.weights.reserve(static_cast<size_t>(right - left));
    float sum = 0.0f;
    for (int64_t i = left; i < right; ++i) {
        float w = triangle_kernel((static_cast<float>(i) - input) / sratio);
        taps.weights.push_back(w);
        sum += w;
    }
    for (float& w : taps.weights) w /= sum;
    return taps;
}

/// A 16-bit channel value in 8 bits, rounded: round(c * 255 / 65535).
uint8_t u16_to_u8(float value) {
    return static_cast<uint8_t>((static_cast<uint32_t>(value) + 128) / 257);
}

/// The resize itself, for 8- or 16-bit samples (no test of the depth per sample). `out` gets
/// width × height × 3 whole numbers 0..max.
template <typename T>
void resize(std::span<const T> source, uint32_t in_width, uint32_t in_height, uint32_t width, uint32_t height,
            float max, std::vector<float>& out) {
    if (in_width == width && in_height == height) {
        for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<float>(source[i]);  // same size: a copy
        return;
    }

    // 1. Vertical pass: in_width × height, kept as unrounded floats.
    std::vector<float> tall(static_cast<size_t>(in_width) * height * 3);
    for (uint32_t y = 0; y < height; ++y) {
        Taps taps = taps_for(y, in_height, height);
        float* row = &tall[static_cast<size_t>(y) * in_width * 3];
        for (uint32_t x = 0; x < in_width; ++x) {
            float t[3] = {0.0f, 0.0f, 0.0f};
            for (size_t i = 0; i < taps.weights.size(); ++i) {
                const T* src = &source[((taps.first + i) * in_width + x) * 3];
                for (int c = 0; c < 3; ++c) t[c] += static_cast<float>(src[c]) * taps.weights[i];
            }
            for (int c = 0; c < 3; ++c) row[x * 3 + c] = t[c];
        }
    }

    // 2. Horizontal pass: width × height, clamped to 0..max and rounded.
    for (uint32_t x = 0; x < width; ++x) {
        Taps taps = taps_for(x, in_width, width);
        for (uint32_t y = 0; y < height; ++y) {
            float t[3] = {0.0f, 0.0f, 0.0f};
            const float* src = &tall[(static_cast<size_t>(y) * in_width + taps.first) * 3];
            for (size_t i = 0; i < taps.weights.size(); ++i) {
                for (int c = 0; c < 3; ++c) t[c] += src[i * 3 + c] * taps.weights[i];
            }
            float* dst = &out[(static_cast<size_t>(y) * width + x) * 3];
            for (int c = 0; c < 3; ++c) dst[c] = std::round(std::clamp(t[c], 0.0f, max));
        }
    }
}

}  // namespace

std::vector<std::array<uint8_t, 3>> resize_to_rgb8(const Picture& image, uint32_t width, uint32_t height) {
    std::vector<float> out(static_cast<size_t>(width) * height * 3, 0.0f);  // whole numbers 0..max
    size_t values = static_cast<size_t>(image.width) * image.height * 3;
    bool sixteen = image.is_16_bit();
    bool consistent = values > 0 && (sixteen ? image.rgb16.size() : image.rgb8.size()) == values;
    if (consistent && sixteen) {
        resize<uint16_t>(image.rgb16, image.width, image.height, width, height, 65535.0f, out);
    } else if (consistent) {
        resize<uint8_t>(image.rgb8, image.width, image.height, width, height, 255.0f, out);
    }
    // else: nothing to sample, a black picture

    // To 8 bits per channel.
    std::vector<std::array<uint8_t, 3>> pixels(static_cast<size_t>(width) * height);
    for (size_t p = 0; p < pixels.size(); ++p) {
        for (int c = 0; c < 3; ++c) {
            float v = out[p * 3 + c];
            pixels[p][c] = sixteen ? u16_to_u8(v) : static_cast<uint8_t>(v);
        }
    }
    return pixels;
}

}  // namespace wpc
