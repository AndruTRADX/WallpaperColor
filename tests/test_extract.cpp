#include <doctest/doctest.h>

#include <filesystem>
#include <functional>
#include <unistd.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include "core/extract.hpp"

using namespace wpc;

namespace {

using PixelFn = std::function<std::array<uint8_t, 3>(uint32_t x, uint32_t y)>;

/// A 200 × 100 picture painted by `f`.
Image image_from(const PixelFn& f, uint32_t width = 200, uint32_t height = 100) {
    Image image{width, height, 255, {}};
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            std::array<uint8_t, 3> p = f(x, y);
            image.rgb.insert(image.rgb.end(), {float(p[0]), float(p[1]), float(p[2])});
        }
    }
    return image;
}

Image solid(uint8_t r, uint8_t g, uint8_t b) {
    return image_from([=](uint32_t, uint32_t) { return std::array<uint8_t, 3>{r, g, b}; });
}

Extraction run(const Image& image) {
    return extract(image, ExtractConfig{});
}

bool between(float v, float lo, float hi) {
    return v >= lo && v < hi;
}

}  // namespace

TEST_CASE("solid colour is kept and brightened") {
    Extraction e = run(solid(200, 20, 20));
    CHECK(e.picked == Rgb{200, 20, 20});
    Hsv hsv = e.led.to_hsv();
    CHECK((hsv.h < 2.0f || hsv.h > 358.0f));  // still red
    CHECK(hsv.v == 1.0f);
}

TEST_CASE("pastel is saturated") {
    Extraction e = run(solid(180, 200, 255));
    CHECK(e.led.to_hsv().s >= float(ExtractConfig{}.min_saturation) - 0.01f);
}

TEST_CASE("grey picture gives white") {
    Extraction e = run(image_from([](uint32_t x, uint32_t) {
        uint8_t v = uint8_t(x % 256);
        return std::array<uint8_t, 3>{v, v, v};
    }));
    CHECK(e.led == WHITE);
}

TEST_CASE("specks are ignored") {
    // grey picture with a 0.16 % orange speck: white, not orange
    Extraction e = run(image_from([](uint32_t x, uint32_t y) {
        if (x < 4 && y < 8) return std::array<uint8_t, 3>{200, 120, 60};
        return std::array<uint8_t, 3>{90, 90, 92};
    }));
    CHECK(e.led == WHITE);
}

TEST_CASE("pale skin on grey gives white") {
    Extraction e = run(image_from([](uint32_t x, uint32_t) {
        if (x < 30) return std::array<uint8_t, 3>{153, 139, 127};
        return std::array<uint8_t, 3>{33, 40, 39};
    }));
    CHECK(e.led == WHITE);
}

TEST_CASE("navy background beats pink hair") {
    Extraction e = run(image_from([](uint32_t x, uint32_t) {
        if (x < 60) return std::array<uint8_t, 3>{64, 64, 82};
        if (x < 75) return std::array<uint8_t, 3>{138, 98, 101};
        return std::array<uint8_t, 3>{21, 21, 30};
    }));
    CHECK(between(e.led.to_hsv().h, 220.0f, 260.0f));
}

TEST_CASE("skin loses to a smaller real colour") {
    // 60 % skin, 15 % blue hair, rest dark: blue
    Extraction e = run(image_from([](uint32_t x, uint32_t) {
        if (x < 120) return std::array<uint8_t, 3>{230, 190, 160};
        if (x < 150) return std::array<uint8_t, 3>{60, 90, 200};
        return std::array<uint8_t, 3>{30, 30, 35};
    }));
    CHECK(between(e.led.to_hsv().h, 215.0f, 240.0f));
}

TEST_CASE("skin only picture still gets a colour") {
    CHECK(run(solid(230, 190, 160)).led != WHITE);
}

TEST_CASE("light pastel stays pastel") {
    // pale peach (12 % saturation, bright): soft peach, not orange
    Extraction e = run(solid(226, 207, 200));
    CHECK(between(e.led.to_hsv().s, 0.25f, 0.4f));
    for (Rgb c : e.arc) CHECK(c.to_hsv().v >= 0.69f);  // no deep shade
}

TEST_CASE("dark muted colour is still boosted") {
    // shaded rose (pink hair in shadow): raised to the saturation floor
    Extraction e = run(solid(95, 53, 54));
    CHECK(e.led.to_hsv().s >= float(ExtractConfig{}.min_saturation) - 0.01f);
}

TEST_CASE("small purple accent beats large dull background") {
    Extraction e = run(image_from([](uint32_t x, uint32_t) {
        if (x < 60) return std::array<uint8_t, 3>{61, 63, 79};     // dull slate, 30 %
        if (x < 100) return std::array<uint8_t, 3>{88, 124, 100};  // green
        if (x < 120) return std::array<uint8_t, 3>{175, 96, 100};  // red (main)
        if (x < 122) return std::array<uint8_t, 3>{206, 86, 218};  // purple sparkle, 1 %
        return std::array<uint8_t, 3>{34, 37, 39};
    }));
    bool purple = false;
    for (Rgb c : e.arc) purple = purple || between(c.to_hsv().h, 270.0f, 310.0f);
    CHECK(purple);
}

TEST_CASE("tinted near black is grey") {
    // mostly an almost-black with a faint purple tint (high saturation, tiny chroma): white
    Extraction e = run(image_from([](uint32_t x, uint32_t) {
        if (x < 100) return std::array<uint8_t, 3>{27, 25, 30};
        return std::array<uint8_t, 3>{70, 72, 75};
    }));
    CHECK_FALSE(e.picked);
    CHECK(e.led == WHITE);
}

TEST_CASE("dark but vivid colour counts") {
    CHECK(between(run(solid(3, 19, 105)).led.to_hsv().h, 225.0f, 240.0f));
}

TEST_CASE("vivid minority beats grey majority") {
    // 80 % grey, 20 % blue: the LEDs should be blue, not white
    Extraction e = run(image_from([](uint32_t x, uint32_t) {
        if (x < 160) return std::array<uint8_t, 3>{120, 120, 125};
        return std::array<uint8_t, 3>{20, 60, 230};
    }));
    CHECK(between(e.led.to_hsv().h, 200.0f, 250.0f));
}

TEST_CASE("palette shares sum to one") {
    Extraction e = run(image_from([](uint32_t x, uint32_t y) {
        return std::array<uint8_t, 3>{uint8_t(x % 256), uint8_t(y * 2 % 256), 90};
    }));
    float sum = 0;
    for (const PaletteEntry& p : e.palette) sum += p.share;
    CHECK(std::abs(sum - 1.0f) < 1e-4f);
    CHECK(e.palette.size() <= 10);
    for (size_t i = 1; i < e.palette.size(); ++i) CHECK(e.palette[i - 1].share >= e.palette[i].share);
}

TEST_CASE("format comes from content not extension") {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("wpc-test-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    std::filesystem::path path = dir / "really-a-png.jpg";
    std::vector<uint8_t> blue;
    for (int i = 0; i < 16; ++i) blue.insert(blue.end(), {0, 0, 255});
    REQUIRE(stbi_write_png(path.c_str(), 4, 4, 3, blue.data(), 4 * 3));
    Extraction e = extract_path(path, ExtractConfig{});
    std::filesystem::remove_all(dir);
    CHECK(e.led == Rgb{0, 0, 255});
}

TEST_CASE("arc takes three distinct hues") {
    Extraction e = run(image_from([](uint32_t x, uint32_t) {
        if (x < 50) return std::array<uint8_t, 3>{220, 30, 30};   // red
        if (x < 60) return std::array<uint8_t, 3>{230, 60, 40};   // another red: too close, skipped
        if (x < 110) return std::array<uint8_t, 3>{30, 60, 220};  // blue
        if (x < 150) return std::array<uint8_t, 3>{40, 200, 60};  // green
        return std::array<uint8_t, 3>{20, 20, 20};
    }));
    CHECK(e.arc[0] == e.led);
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            CHECK(hue_distance(e.arc[i].to_hsv().h, e.arc[j].to_hsv().h) >= ARC_MIN_HUE_GAP);
        }
    }
}

TEST_CASE("single colour picture gets shades in the arc") {
    Extraction e = run(solid(30, 60, 220));
    Hsv a = e.arc[0].to_hsv();
    Hsv b = e.arc[1].to_hsv();
    Hsv c = e.arc[2].to_hsv();
    CHECK(hue_distance(a.h, b.h) < 5.0f);
    CHECK(hue_distance(a.h, c.h) < 5.0f);
    CHECK(b.s < a.s);  // lighter shade
    CHECK(c.v < a.v);  // dimmer shade
}

TEST_CASE("colourless picture arc is white") {
    Extraction e = run(image_from([](uint32_t x, uint32_t) {
        uint8_t v = uint8_t(x % 256);
        return std::array<uint8_t, 3>{v, v, v};
    }));
    CHECK(e.arc == std::array<Rgb, 3>{WHITE, WHITE, WHITE});
}

TEST_CASE("single pixel image") {
    Image one = image_from([](uint32_t, uint32_t) { return std::array<uint8_t, 3>{0, 255, 0}; }, 1, 1);
    CHECK(run(one).led == Rgb{0, 255, 0});
}
