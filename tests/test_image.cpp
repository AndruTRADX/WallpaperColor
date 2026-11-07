#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unistd.h>

#include <png.h>
#include <stb/stb_image_write.h>  // implemented in test_extract.cpp

#include "shared/domain/colours/picture.hpp"
#include "shared/infrastructure/services/picture_files.hpp"
#include "utils/error.hpp"
#include "utils/picture_decoding.hpp"

using namespace wpc;
using namespace std::literals;
namespace fs = std::filesystem;

namespace {

using Rgb8 = std::array<uint8_t, 3>;
using PixelFn = std::function<Rgb8(uint32_t x, uint32_t y)>;

/// A folder of its own, deleted at the end of the test.
struct TempDir {
    fs::path path = fs::temp_directory_path() / ("wpc-image-test-" + std::to_string(getpid()));
    TempDir() { fs::create_directories(path); }
    ~TempDir() { fs::remove_all(path); }
    fs::path operator/(const char* name) const { return path / name; }
};

std::vector<uint8_t> pixels(uint32_t width, uint32_t height, const PixelFn& f) {
    std::vector<uint8_t> out;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            Rgb8 p = f(x, y);
            out.insert(out.end(), p.begin(), p.end());
        }
    }
    return out;
}

/// Red on the left half, blue on the right.
Rgb8 halves(uint32_t x, uint32_t width) {
    return x < width / 2 ? Rgb8{255, 0, 0} : Rgb8{0, 0, 255};
}

/// Pixel (x, y) of an 8-bit picture.
Rgb8 at(const Picture& image, uint32_t x, uint32_t y) {
    size_t i = (static_cast<size_t>(y) * image.width + x) * 3;
    return {image.rgb8[i], image.rgb8[i + 1], image.rgb8[i + 2]};
}

bool near(Rgb8 a, Rgb8 b, int tolerance) {
    for (int c = 0; c < 3; ++c) {
        if (std::abs(int(a[c]) - int(b[c])) > tolerance) return false;
    }
    return true;
}

/// A 16-bit RGB PNG through libpng (stb_image_write only writes 8 bits).
void write_png16(const fs::path& path, uint32_t width, uint32_t height, bool interlaced,
                 const std::function<std::array<uint16_t, 3>(uint32_t, uint32_t)>& f) {
    FILE* file = std::fopen(path.c_str(), "wb");
    REQUIRE(file);
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    png_init_io(png, file);
    png_set_IHDR(png, info, width, height, 16, PNG_COLOR_TYPE_RGB,
                 interlaced ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_set_swap(png);  // our values are little-endian, PNG's big-endian
    std::vector<uint16_t> all;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            std::array<uint16_t, 3> p = f(x, y);
            all.insert(all.end(), p.begin(), p.end());
        }
    }
    std::vector<png_bytep> rows(height);
    for (uint32_t y = 0; y < height; ++y) rows[y] = reinterpret_cast<png_bytep>(&all[y * width * 3]);
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(file);
}

}  // namespace

TEST_CASE("shrink factor keeps both sides at least min_side") {
    CHECK(shrink_factor(3840, 2160, 0) == 1);     // 0 = full size
    CHECK(shrink_factor(3840, 2160, 384) == 5);   // 2160 / 5 = 432 ≥ 384
    CHECK(shrink_factor(1920, 1080, 384) == 2);
    CHECK(shrink_factor(300, 200, 384) == 1);     // already smaller: kept
    CHECK(shrink_factor(200, 3000, 100) == 2);    // the smaller side decides
}

TEST_CASE("big PNG is shrunk while it is read") {
    TempDir dir;
    fs::path path = dir / "halves.png";
    std::vector<uint8_t> data = pixels(800, 400, [](uint32_t x, uint32_t) { return halves(x, 800); });
    REQUIRE(stbi_write_png(path.c_str(), 800, 400, 3, data.data(), 800 * 3));

    Picture full = PictureFiles().read(path);
    CHECK(full.width == 800);
    CHECK(full.height == 400);

    Picture small = PictureFiles().read(path, 100);  // factor 4
    CHECK(small.width == 200);
    CHECK(small.height == 100);
    CHECK(at(small, 0, 0) == Rgb8{255, 0, 0});
    CHECK(at(small, 199, 99) == Rgb8{0, 0, 255});
}

TEST_CASE("blocks cut short at the edges average what they have") {
    TempDir dir;
    fs::path path = dir / "odd.png";
    // 5 × 3: the last column is white, the rest black.
    std::vector<uint8_t> data =
        pixels(5, 3, [](uint32_t x, uint32_t) { return x == 4 ? Rgb8{255, 255, 255} : Rgb8{0, 0, 0}; });
    REQUIRE(stbi_write_png(path.c_str(), 5, 3, 3, data.data(), 5 * 3));

    Picture small = PictureFiles().read(path, 1);  // factor 3: 2 × 1 blocks of 3 × 3 and 2 × 3 pixels
    REQUIRE(small.width == 2);
    REQUIRE(small.height == 1);
    CHECK(at(small, 0, 0) == Rgb8{0, 0, 0});
    CHECK(at(small, 1, 0) == Rgb8{128, 128, 128});  // half white: 127.5 rounded
}

TEST_CASE("PNG grey and alpha become plain RGB") {
    TempDir dir;
    fs::path grey = dir / "grey.png";
    uint8_t g[4] = {10, 10, 10, 10};
    REQUIRE(stbi_write_png(grey.c_str(), 2, 2, 1, g, 2));
    CHECK(at(PictureFiles().read(grey), 1, 1) == Rgb8{10, 10, 10});

    fs::path alpha = dir / "alpha.png";
    uint8_t a[16] = {1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0};  // fully transparent
    REQUIRE(stbi_write_png(alpha.c_str(), 2, 2, 4, a, 8));
    CHECK(at(PictureFiles().read(alpha), 0, 0) == Rgb8{1, 2, 3});  // alpha is ignored, not blended
}

TEST_CASE("16-bit PNG stays 16-bit, plain and interlaced") {
    TempDir dir;
    for (bool interlaced : {false, true}) {
        fs::path path = dir / (interlaced ? "interlaced.png" : "plain.png");
        write_png16(path, 64, 32, interlaced, [](uint32_t x, uint32_t) {
            return x < 32 ? std::array<uint16_t, 3>{65535, 0, 1000} : std::array<uint16_t, 3>{0, 40000, 0};
        });
        Picture image = PictureFiles().read(path, 8);  // factor 4
        CHECK(image.is_16_bit());
        CHECK(image.width == 16);
        CHECK(image.height == 8);
        CHECK(image.rgb16[0] == 65535);
        CHECK(image.rgb16[2] == 1000);
        CHECK(image.rgb16[(16 - 1) * 3 + 1] == 40000);
    }
}

TEST_CASE("big JPEG is decoded at a fraction of its size") {
    TempDir dir;
    fs::path path = dir / "halves.jpg";
    std::vector<uint8_t> data = pixels(800, 400, [](uint32_t x, uint32_t) { return halves(x, 800); });
    REQUIRE(stbi_write_jpg(path.c_str(), 800, 400, 3, data.data(), 95));

    Picture small = PictureFiles().read(path, 100);  // factor 4: libjpeg decodes at 1/4
    CHECK(small.width == 200);
    CHECK(small.height == 100);
    CHECK(near(at(small, 10, 50), {255, 0, 0}, 12));   // JPEG is lossy
    CHECK(near(at(small, 190, 50), {0, 0, 255}, 12));

    Picture tiny = PictureFiles().read(path, 10);  // factor 40: 1/8 is as far as libjpeg goes
    CHECK(tiny.width == 100);
}

TEST_CASE("BMP still loads") {
    TempDir dir;
    fs::path path = dir / "green.bmp";
    std::vector<uint8_t> data = pixels(4, 4, [](uint32_t, uint32_t) { return Rgb8{0, 200, 0}; });
    REQUIRE(stbi_write_bmp(path.c_str(), 4, 4, 3, data.data()));
    Picture image = PictureFiles().read(path, 2);
    CHECK(image.width == 4);  // stb_image pictures are not shrunk
    CHECK(at(image, 3, 3) == Rgb8{0, 200, 0});
}

TEST_CASE("damaged files are errors, not crashes") {
    TempDir dir;
    auto write = [&](const char* name, std::string_view bytes) {
        std::ofstream(dir / name, std::ios::binary) << bytes;
        return dir / name;
    };
    CHECK_THROWS_AS(PictureFiles().read(write("bad.jpg", "\xFF\xD8\xFF\xE0 not really a jpeg"sv)), Error);
    CHECK_THROWS_AS(PictureFiles().read(write("bad.png", "\x89PNG\r\n\x1A\n then nothing"sv)), Error);
    CHECK_THROWS_AS(PictureFiles().read(write("bad.webp", "RIFF\0\0\0\0WEBPVP8 "sv)), Error);
    CHECK_THROWS_AS(PictureFiles().read(write("empty.png", "")), Error);
    CHECK_THROWS_AS(PictureFiles().read(dir / "missing.png"), Error);

    // A PNG cut short in the middle of its pixels.
    fs::path path = dir / "cut.png";
    std::vector<uint8_t> data = pixels(300, 300, [](uint32_t x, uint32_t y) {
        return Rgb8{uint8_t(x * 7), uint8_t(y * 13), uint8_t(x ^ y)};
    });
    REQUIRE(stbi_write_png(path.c_str(), 300, 300, 3, data.data(), 300 * 3));
    fs::resize_file(path, fs::file_size(path) / 2);
    CHECK_THROWS_AS(PictureFiles().read(path, 10), Error);
}

namespace {

/// Whether a file named `name` is mapped into this process.
bool mapped(std::string_view name) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        if (line.ends_with(name)) return true;
    }
    return false;
}

/// Points WPC_PICTURES_MODULE somewhere else while it exists.
struct PicturesModule {
    explicit PicturesModule(const fs::path& file) { setenv("WPC_PICTURES_MODULE", file.c_str(), 1); }
    ~PicturesModule() { setenv("WPC_PICTURES_MODULE", WPC_BUILT_PICTURES, 1); }
};

}  // namespace

TEST_CASE("the decoders are in memory only while a picture is read") {
    TempDir dir;
    fs::path path = dir / "small.png";
    std::vector<uint8_t> data = pixels(8, 8, [](uint32_t x, uint32_t) { return halves(x, 8); });
    REQUIRE(stbi_write_png(path.c_str(), 8, 8, 3, data.data(), 8 * 3));

    CHECK_FALSE(mapped("/wpc-pictures.so"));
    CHECK(PictureFiles().read(path).width == 8);
    CHECK_FALSE(mapped("/wpc-pictures.so"));
}

// (Named "damaged files…": libjpeg's longjmp, like the test above, is skipped under
// ThreadSanitizer.)
TEST_CASE("damaged files are errors after the decoders are unloaded") {
    // An error thrown in the module arrives as wpc::Error, with its message, and the module is
    // unloaded.
    TempDir dir;
    std::ofstream(dir / "bad.jpg", std::ios::binary) << "\xFF\xD8\xFF\xE0 not really a jpeg"sv;
    try {
        PictureFiles().read(dir / "bad.jpg");
        FAIL("no error");
    } catch (const Error& e) {
        CHECK(std::string_view(e.what()).starts_with("cannot read image: "));
    }
    CHECK_FALSE(mapped("/wpc-pictures.so"));
}

TEST_CASE("pictures need the decoders' module of this build") {
    TempDir dir;
    fs::path path = dir / "small.png";
    std::vector<uint8_t> data = pixels(2, 2, [](uint32_t x, uint32_t) { return halves(x, 2); });
    REQUIRE(stbi_write_png(path.c_str(), 2, 2, 3, data.data(), 2 * 3));
    {
        PicturesModule missing(dir / "wpc-pictures.so");
        CHECK_THROWS_WITH_AS(PictureFiles().read(path), doctest::Contains("cannot load"), Error);
    }
    {
        PicturesModule other(fs::path(WPC_TEST_MODULES) / "wpc-driver-fake.so");  // not the decoders
        CHECK_THROWS_WITH_AS(PictureFiles().read(path), doctest::Contains("has no picture decoders"), Error);
    }
    CHECK(PictureFiles().read(path).width == 2);
}
