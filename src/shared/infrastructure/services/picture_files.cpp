#include "shared/infrastructure/services/picture_files.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <string_view>

#include <jpeglib.h>
#include <png.h>
#include <webp/decode.h>

// stb_image is only used for GIF and BMP now (rare as wallpapers).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include <stb/stb_image.h>

#include "utils/error.hpp"

namespace wpc {

namespace {

struct CloseFile {
    void operator()(FILE* file) const { std::fclose(file); }
};
using File = std::unique_ptr<FILE, CloseFile>;

File open_file(const std::filesystem::path& path) {
    File file(std::fopen(path.c_str(), "rb"));
    if (!file) throw Error("cannot read image: " + os_error(errno));
    return file;
}

std::vector<unsigned char> read_rest(FILE* file) {
    std::vector<unsigned char> data;
    unsigned char buffer[65536];
    while (size_t n = std::fread(buffer, 1, sizeof buffer, file)) data.insert(data.end(), buffer, buffer + n);
    if (std::ferror(file)) throw Error("cannot read image: " + os_error(errno));
    return data;
}

enum class Format { Jpeg, Png, Webp, Other };

/// The format, from the first bytes of the file.
Format sniff(FILE* file) {
    unsigned char head[12] = {};
    size_t n = std::fread(head, 1, sizeof head, file);
    std::rewind(file);
    auto starts_with = [&](std::string_view magic, size_t at = 0) {
        return n >= at + magic.size() && std::memcmp(head + at, magic.data(), magic.size()) == 0;
    };
    if (starts_with("\xFF\xD8\xFF")) return Format::Jpeg;
    if (starts_with("\x89PNG\r\n\x1A\n")) return Format::Png;
    if (starts_with("RIFF") && starts_with("WEBP", 8)) return Format::Webp;
    return Format::Other;
}

/// Shrinks a picture by averaging blocks of factor × factor pixels while its rows arrive,
/// so only one row of the full-size picture exists at a time. `T` is uint8_t or uint16_t.
/// Edge blocks that are cut short average the pixels they have.
template <typename T>
class BlockShrinker {
public:
    BlockShrinker(uint32_t width, uint32_t height, uint32_t factor, std::vector<T>& out)
        : width_(width), factor_(factor), out_width_((width + factor - 1) / factor), out_(out) {
        out_.reserve(static_cast<size_t>(out_width_) * ((height + factor - 1) / factor) * 3);
        sums_.assign(static_cast<size_t>(out_width_) * 3, 0);
    }

    uint32_t out_width() const { return out_width_; }

    /// One row of the picture: width × 3 values.
    void add_row(const T* rgb) {
        for (uint32_t x = 0; x < width_; ++x) {
            for (int c = 0; c < 3; ++c) sums_[(x / factor_) * 3 + c] += rgb[x * 3 + c];
        }
        if (++rows_ == factor_) flush();
    }

    /// After the last row.
    void finish() {
        if (rows_ > 0) flush();
    }

private:
    void flush() {
        for (uint32_t bx = 0; bx < out_width_; ++bx) {
            uint64_t columns = std::min(factor_, width_ - bx * factor_);
            uint64_t count = columns * rows_;
            for (int c = 0; c < 3; ++c) {
                uint64_t& sum = sums_[bx * 3 + c];
                out_.push_back(static_cast<T>((sum + count / 2) / count));  // rounded mean
                sum = 0;
            }
        }
        rows_ = 0;
    }

    uint32_t width_;
    uint32_t factor_;
    uint32_t out_width_;
    std::vector<T>& out_;
    std::vector<uint64_t> sums_;
    uint32_t rows_ = 0;
};

// ---------------------------------------------------------------------------------------------
// libjpeg and libpng report an error by calling a function that must not return. Ours jumps
// back with longjmp to the setjmp in guarded(). The jump must not skip anything with a
// destructor, so every call into those libraries goes through guarded() with a lambda that
// only calls the library.

/// Runs `step`; false if the library reported an error during it.
template <typename Step>
bool guarded(std::jmp_buf& jump, Step step) {
    if (setjmp(jump)) return false;
    step();
    return true;
}

/// A libjpeg decompressor. Its errors become wpc::Error.
class JpegReader {
public:
    JpegReader() {
        info.err = jpeg_std_error(&error_.manager);
        error_.manager.error_exit = fail;
        error_.manager.output_message = [](j_common_ptr) {};  // warnings are not printed
        run([&] { jpeg_create_decompress(&info); });
    }
    ~JpegReader() { jpeg_destroy_decompress(&info); }
    JpegReader(const JpegReader&) = delete;
    JpegReader& operator=(const JpegReader&) = delete;

    template <typename Step>
    void run(Step step) {
        if (!guarded(error_.jump, step)) throw Error(std::format("cannot read image: {}", error_.message));
    }

    jpeg_decompress_struct info{};

private:
    struct ErrorState {
        jpeg_error_mgr manager;  // first: libjpeg's pointer to it also points to the whole struct
        std::jmp_buf jump;
        char message[JMSG_LENGTH_MAX];
    };
    static void fail(j_common_ptr info) {
        ErrorState* error = reinterpret_cast<ErrorState*>(info->err);
        info->err->format_message(info, error->message);
        std::longjmp(error->jump, 1);
    }
    ErrorState error_{};
};

Picture decode_jpeg(FILE* file, uint32_t min_side) {
    JpegReader jpeg;
    jpeg_decompress_struct& info = jpeg.info;
    jpeg.run([&] {
        jpeg_stdio_src(&info, file);
        jpeg_read_header(&info, TRUE);
    });

    // libjpeg can decode at 1/2, 1/4 or 1/8 of the size directly: take the smallest allowed.
    uint32_t factor = shrink_factor(info.image_width, info.image_height, min_side);
    info.scale_num = 1;
    info.scale_denom = factor >= 8 ? 8 : factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    bool cmyk = info.jpeg_color_space == JCS_CMYK || info.jpeg_color_space == JCS_YCCK;
    info.out_color_space = cmyk ? JCS_CMYK : JCS_RGB;  // grey comes out as RGB too
    jpeg.run([&] { jpeg_start_decompress(&info); });

    Picture image{info.output_width, info.output_height, {}, {}};
    image.rgb8.reserve(static_cast<size_t>(image.width) * image.height * 3);
    std::vector<JSAMPLE> row(static_cast<size_t>(image.width) * info.output_components);
    JSAMPROW rows[1] = {row.data()};
    while (info.output_scanline < info.output_height) {
        jpeg.run([&] { jpeg_read_scanlines(&info, rows, 1); });
        if (!cmyk) {
            image.rgb8.insert(image.rgb8.end(), row.begin(), row.end());
            continue;
        }
        // Photoshop writes CMYK inverted (255 = no ink), others not.
        for (uint32_t x = 0; x < image.width; ++x) {
            const JSAMPLE* p = &row[x * 4];
            auto clear = [&](int i) -> uint32_t { return info.saw_Adobe_marker ? p[i] : 255 - p[i]; };
            for (int c = 0; c < 3; ++c) image.rgb8.push_back(static_cast<uint8_t>(clear(c) * clear(3) / 255));
        }
    }
    jpeg.run([&] { jpeg_finish_decompress(&info); });
    return image;
}

// ---------------------------------------------------------------------------------------------

/// A libpng reader. Its errors become wpc::Error.
class PngReader {
public:
    PngReader() {
        png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &error_, fail, [](png_structp, png_const_charp) {});
        if (png) info = png_create_info_struct(png);
        if (!png || !info) {
            png_destroy_read_struct(&png, &info, nullptr);
            throw Error("cannot read image: out of memory");
        }
    }
    ~PngReader() { png_destroy_read_struct(&png, &info, nullptr); }
    PngReader(const PngReader&) = delete;
    PngReader& operator=(const PngReader&) = delete;

    template <typename Step>
    void run(Step step) {
        if (!guarded(error_.jump, step)) throw Error(std::format("cannot read image: {}", error_.message));
    }

    png_structp png = nullptr;
    png_infop info = nullptr;

private:
    struct ErrorState {
        std::jmp_buf jump;
        char message[200];
    };
    static void fail(png_structp png, png_const_charp message) {
        ErrorState* error = static_cast<ErrorState*>(png_get_error_ptr(png));
        std::snprintf(error->message, sizeof error->message, "%s", message);
        std::longjmp(error->jump, 1);
    }
    ErrorState error_{};
};

/// Reads every row into `out`, shrunk by `factor`; returns the shrunk width. `T` is uint8_t
/// or uint16_t, as the PNG's bit depth.
template <typename T>
uint32_t read_png_rows(PngReader& reader, uint32_t width, uint32_t height, bool interlaced, uint32_t factor,
                       std::vector<T>& out) {
    size_t row_values = static_cast<size_t>(width) * 3;
    BlockShrinker<T> shrinker(width, height, factor, out);
    if (interlaced) {
        // Each row is only complete after the last pass, so the whole picture is needed.
        std::vector<T> all(row_values * height);
        std::vector<png_bytep> pointers(height);
        for (uint32_t y = 0; y < height; ++y) pointers[y] = reinterpret_cast<png_bytep>(&all[y * row_values]);
        reader.run([&] { png_read_image(reader.png, pointers.data()); });
        for (uint32_t y = 0; y < height; ++y) shrinker.add_row(&all[y * row_values]);
    } else {
        std::vector<T> row(row_values);
        png_bytep bytes = reinterpret_cast<png_bytep>(row.data());
        for (uint32_t y = 0; y < height; ++y) {
            reader.run([&] { png_read_row(reader.png, bytes, nullptr); });
            shrinker.add_row(row.data());
        }
    }
    shrinker.finish();
    return shrinker.out_width();
}

Picture decode_png(FILE* file, uint32_t min_side) {
    PngReader reader;
    bool sixteen = false;
    bool interlaced = false;
    reader.run([&] {
        png_init_io(reader.png, file);
        png_read_info(reader.png, reader.info);
        // Always red, green, blue: palettes and grey expanded, alpha dropped.
        png_set_expand(reader.png);
        png_set_gray_to_rgb(reader.png);
        png_set_strip_alpha(reader.png);
        sixteen = png_get_bit_depth(reader.png, reader.info) == 16;
        if (sixteen) png_set_swap(reader.png);  // PNG stores 16-bit values big-endian
        interlaced = png_set_interlace_handling(reader.png) > 1;
        png_read_update_info(reader.png, reader.info);
    });
    uint32_t width = png_get_image_width(reader.png, reader.info);
    uint32_t height = png_get_image_height(reader.png, reader.info);
    if (png_get_rowbytes(reader.png, reader.info) != static_cast<size_t>(width) * 3 * (sixteen ? 2 : 1)) {
        throw Error("cannot read image: unexpected PNG pixel layout");
    }

    uint32_t factor = shrink_factor(width, height, min_side);
    Picture image;
    image.width = sixteen ? read_png_rows(reader, width, height, interlaced, factor, image.rgb16)
                          : read_png_rows(reader, width, height, interlaced, factor, image.rgb8);
    image.height = (height + factor - 1) / factor;
    reader.run([&] { png_read_end(reader.png, nullptr); });
    return image;
}

// ---------------------------------------------------------------------------------------------

Picture decode_webp(FILE* file, uint32_t min_side) {
    std::vector<unsigned char> data = read_rest(file);
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config) || WebPGetFeatures(data.data(), data.size(), &config.input) != VP8_STATUS_OK) {
        throw Error("cannot read image: invalid WebP data");
    }
    uint32_t width = static_cast<uint32_t>(config.input.width);
    uint32_t height = static_cast<uint32_t>(config.input.height);
    uint32_t factor = shrink_factor(width, height, min_side);
    if (factor > 1) {
        config.options.use_scaling = 1;
        config.options.scaled_width = static_cast<int>((width + factor - 1) / factor);
        config.options.scaled_height = static_cast<int>((height + factor - 1) / factor);
    }
    config.output.colorspace = MODE_RGB;
    if (WebPDecode(data.data(), data.size(), &config) != VP8_STATUS_OK) {
        WebPFreeDecBuffer(&config.output);
        throw Error("cannot read image: invalid WebP data");
    }
    const WebPRGBABuffer& out = config.output.u.RGBA;
    Picture image{static_cast<uint32_t>(config.output.width), static_cast<uint32_t>(config.output.height), {}, {}};
    image.rgb8.reserve(static_cast<size_t>(image.width) * image.height * 3);
    for (uint32_t y = 0; y < image.height; ++y) {
        const uint8_t* row = out.rgba + static_cast<size_t>(y) * out.stride;
        image.rgb8.insert(image.rgb8.end(), row, row + static_cast<size_t>(image.width) * 3);
    }
    WebPFreeDecBuffer(&config.output);
    return image;
}

/// GIF and BMP, through stb_image, at full size.
Picture decode_other(FILE* file) {
    std::vector<unsigned char> data = read_rest(file);
    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels_in_file, 3),
        &stbi_image_free);
    if (!pixels) throw Error(std::format("cannot read image: {}", stbi_failure_reason()));
    Picture image{static_cast<uint32_t>(width), static_cast<uint32_t>(height), {}, {}};
    image.rgb8.assign(pixels.get(), pixels.get() + static_cast<size_t>(width) * height * 3);
    return image;
}

}  // namespace


Picture PictureFiles::read(const std::filesystem::path& path, uint32_t min_side) const {
    File file = open_file(path);
    switch (sniff(file.get())) {
        case Format::Jpeg: return decode_jpeg(file.get(), min_side);
        case Format::Png: return decode_png(file.get(), min_side);
        case Format::Webp: return decode_webp(file.get(), min_side);
        case Format::Other: return decode_other(file.get());
    }
    return {};
}

std::optional<int64_t> PictureFiles::modified_time(const std::filesystem::path& path) const {
    std::error_code ec;
    std::filesystem::file_time_type t = std::filesystem::last_write_time(path, ec);
    if (ec) return std::nullopt;
    return static_cast<int64_t>(t.time_since_epoch().count());
}

}  // namespace wpc
