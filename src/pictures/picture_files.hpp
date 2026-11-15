// Reading picture files (JPEG, PNG, WebP, GIF, BMP), at full size or smaller.
//
// PictureReader is the port both programs read pictures through (the tests give a fake one);
// PictureFiles reads them with the decoders' module, wpc-pictures.so, loaded only while a
// picture is read: the rest of the time the image libraries take no memory. How the
// decoding works: decoders.hpp.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include "colours/picture.hpp"

namespace wpc {

class PictureReader {
public:
    virtual ~PictureReader() = default;
    /// The picture at `path`, both sides shrunk by the same factor as far as possible while
    /// staying at least `min_side` pixels (0: full size). Throws wpc::Error.
    virtual Picture read(const std::filesystem::path& path, uint32_t min_side) = 0;
    /// When the file was last changed (a number that grows with time); nullopt if it can't be
    /// read.
    virtual std::optional<int64_t> modified_time(const std::filesystem::path& path) = 0;
};

class PictureFiles final : public PictureReader {
public:
    /// `module`: the decoders' module file (default_decoders_module()).
    explicit PictureFiles(std::filesystem::path module) : module_(std::move(module)) {}

    Picture read(const std::filesystem::path& path, uint32_t min_side) override;
    std::optional<int64_t> modified_time(const std::filesystem::path& path) override;

private:
    std::filesystem::path module_;
};

/// Where the decoders' module of this program is: next to it (the build folder, the release
/// tarball) or in ../lib/wallpapercolor/ (installed). The installed place when neither exists:
/// reading a picture then says it is missing.
std::filesystem::path default_decoders_module();

}  // namespace wpc
