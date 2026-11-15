#include "pictures/picture_files.hpp"

#include <cstring>
#include <format>

#include "pictures/decoders.hpp"
#include "utils/error.hpp"
#include "utils/files.hpp"
#include "utils/shared_library.hpp"

namespace wpc {

namespace fs = std::filesystem;

namespace {

constexpr const char* MODULE = "wpc-pictures.so";

}  // namespace

fs::path default_decoders_module() {
    std::optional<fs::path> folder = program_folder();
    if (!folder) return fs::path(MODULE);
    fs::path next_to = *folder / MODULE;
    std::error_code ec;
    if (fs::exists(next_to, ec)) return next_to;
    return folder->parent_path() / "lib" / "wallpapercolor" / MODULE;
}

Picture PictureFiles::read(const fs::path& path, uint32_t min_side) {
    // Loaded for this picture only, unloaded when this function returns.
    std::optional<SharedLibrary> module;
    try {
        module.emplace(module_);
    } catch (const Error& e) {
        throw Error(std::format("cannot read image: {}: run install.sh again", e.what()));
    }
    auto entry = module->function<const PictureDecoder* (*)()>("wpc_picture_decoder");
    const PictureDecoder* decoder = entry ? entry() : nullptr;
    if (!decoder || std::strcmp(decoder->built_for, WPC_BUILD_ID) != 0) {
        throw Error(std::format("cannot read image: {} {}: run install.sh again", module_.string(),
                                decoder ? "is from another build" : "has no picture decoders"));
    }

    DecodedPicture decoded;
    try {
        decoded = decoder->read(path, min_side);
    } catch (const Error& e) {
        // The exception was made by the module's code, which goes when the module is unloaded:
        // a copy made here leaves this function instead.
        throw Error(e.what());
    } catch (const std::bad_alloc&) {
        throw Error("cannot read image: out of memory");
    } catch (const std::exception& e) {
        throw Error(std::format("cannot read image: {}", e.what()));
    }
    return Picture{decoded.width, decoded.height, std::move(decoded.rgb8), std::move(decoded.rgb16)};
}

std::optional<int64_t> PictureFiles::modified_time(const fs::path& path) {
    std::error_code ec;
    fs::file_time_type t = fs::last_write_time(path, ec);
    if (ec) return std::nullopt;
    return static_cast<int64_t>(t.time_since_epoch().count());
}

}  // namespace wpc
