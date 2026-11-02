#include "core/image.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>

#include <webp/decode.h>

// stb_image decodes JPEG, PNG, GIF and BMP (WebP goes through libwebp below). Other formats
// are left out.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include <stb/stb_image.h>

#include "util/error.hpp"

namespace wpc {

static std::vector<unsigned char> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw Error("cannot read image: " + os_error(errno));
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static bool is_webp(const std::vector<unsigned char>& data) {
    return data.size() >= 12 && std::equal(data.begin(), data.begin() + 4, "RIFF") &&
           std::equal(data.begin() + 8, data.begin() + 12, "WEBP");
}

static Image decode_webp(const std::vector<unsigned char>& data) {
    int width = 0;
    int height = 0;
    std::unique_ptr<uint8_t, decltype(&WebPFree)> pixels(
        WebPDecodeRGB(data.data(), data.size(), &width, &height), &WebPFree);
    if (!pixels) throw Error("cannot read image: invalid WebP data");

    Image image{static_cast<uint32_t>(width), static_cast<uint32_t>(height), {}, {}};
    image.rgb8.assign(pixels.get(), pixels.get() + static_cast<size_t>(width) * height * 3);
    return image;
}

static Image decode_with_stb(const std::vector<unsigned char>& data) {
    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    int size = static_cast<int>(data.size());
    Image image;

    // 16-bit PNGs stay 16-bit until after the resize, which is more precise.
    if (stbi_is_16_bit_from_memory(data.data(), size)) {
        std::unique_ptr<stbi_us, decltype(&stbi_image_free)> pixels(
            stbi_load_16_from_memory(data.data(), size, &width, &height, &channels_in_file, 3),
            &stbi_image_free);
        if (!pixels) throw Error(std::format("cannot read image: {}", stbi_failure_reason()));
        image.rgb16.assign(pixels.get(), pixels.get() + static_cast<size_t>(width) * height * 3);
    } else {
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(data.data(), size, &width, &height, &channels_in_file, 3),
            &stbi_image_free);
        if (!pixels) throw Error(std::format("cannot read image: {}", stbi_failure_reason()));
        image.rgb8.assign(pixels.get(), pixels.get() + static_cast<size_t>(width) * height * 3);
    }
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    return image;
}

Image load_image(const std::filesystem::path& path) {
    std::vector<unsigned char> data = read_file(path);
    if (is_webp(data)) return decode_webp(data);
    return decode_with_stb(data);
}

// ---------------------------------------------------------------------------------------------
// Resize: a separable triangle filter. First every column is resampled to the new height
// (kept as unrounded floats), then every row to the new width. Each output pixel is the
// weighted average of the source pixels under a triangle centred on it, as wide as one output
// pixel on each side when shrinking.

static float triangle_kernel(float x) {
    return (std::abs(x) < 1.0f) ? 1.0f - std::abs(x) : 0.0f;
}

/// Weights of the source pixels that make output pixel `out` along one axis, and the first of
/// those source pixels. `in_size` source pixels become `out_size`.
struct Taps {
    uint32_t first = 0;
    std::vector<float> weights;
};

static Taps taps_for(uint32_t out, uint32_t in_size, uint32_t out_size) {
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
static uint8_t u16_to_u8(float value) {
    return static_cast<uint8_t>((static_cast<uint32_t>(value) + 128) / 257);
}

std::vector<std::array<uint8_t, 3>> resize_to_rgb8(const Image& image, uint32_t width,
                                                   uint32_t height) {
    const float max = image.max();
    std::vector<float> out(static_cast<size_t>(width) * height * 3, 0.0f);  // whole numbers 0..max

    if (image.width == 0 || image.height == 0) {
        // nothing to sample: a black picture
    } else if (image.width == width && image.height == height) {
        for (size_t i = 0; i < out.size(); ++i) out[i] = image.sample(i);  // same size: a plain copy
    } else {
        // 1. Vertical pass: image.width × height, kept as unrounded floats.
        std::vector<float> tall(static_cast<size_t>(image.width) * height * 3);
        for (uint32_t y = 0; y < height; ++y) {
            Taps taps = taps_for(y, image.height, height);
            for (uint32_t x = 0; x < image.width; ++x) {
                float t[3] = {0.0f, 0.0f, 0.0f};
                for (size_t i = 0; i < taps.weights.size(); ++i) {
                    size_t src = ((taps.first + i) * image.width + x) * 3;
                    for (int c = 0; c < 3; ++c) t[c] += image.sample(src + c) * taps.weights[i];
                }
                size_t dst = (static_cast<size_t>(y) * image.width + x) * 3;
                for (int c = 0; c < 3; ++c) tall[dst + c] = t[c];
            }
        }

        // 2. Horizontal pass: width × height, clamped to 0..max and rounded.
        for (uint32_t x = 0; x < width; ++x) {
            Taps taps = taps_for(x, image.width, width);
            for (uint32_t y = 0; y < height; ++y) {
                float t[3] = {0.0f, 0.0f, 0.0f};
                for (size_t i = 0; i < taps.weights.size(); ++i) {
                    size_t src = (static_cast<size_t>(y) * image.width + taps.first + i) * 3;
                    for (int c = 0; c < 3; ++c) t[c] += tall[src + c] * taps.weights[i];
                }
                size_t dst = (static_cast<size_t>(y) * width + x) * 3;
                for (int c = 0; c < 3; ++c) {
                    out[dst + c] = std::round(std::clamp(t[c], 0.0f, max));
                }
            }
        }
    }

    // 3. To 8 bits per channel.
    std::vector<std::array<uint8_t, 3>> pixels(static_cast<size_t>(width) * height);
    for (size_t p = 0; p < pixels.size(); ++p) {
        for (int c = 0; c < 3; ++c) {
            float v = out[p * 3 + c];
            pixels[p][c] = image.is_16_bit() ? u16_to_u8(v) : static_cast<uint8_t>(v);
        }
    }
    return pixels;
}

}  // namespace wpc
