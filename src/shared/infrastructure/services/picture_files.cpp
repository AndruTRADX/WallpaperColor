#include "shared/infrastructure/services/picture_files.hpp"

#include <cstdlib>
#include <cstring>
#include <format>

#include <dlfcn.h>

#include "utils/error.hpp"
#include "utils/picture_decoding.hpp"

namespace wpc {

namespace fs = std::filesystem;

namespace {

constexpr const char* MODULE = "wpc-pictures.so";

/// The decoders' module: $WPC_PICTURES_MODULE (the tests), next to the program (the build
/// folder), or in ../lib/wallpapercolor/ (installed).
fs::path module_file() {
    if (const char* file = std::getenv("WPC_PICTURES_MODULE")) return file;
    std::error_code ec;
    fs::path program = fs::read_symlink("/proc/self/exe", ec);
    if (ec) throw Error(std::format("cannot read image: cannot find {}: {}", MODULE, ec.message()));
    for (const fs::path& file : {program.parent_path() / MODULE,
                                 program.parent_path().parent_path() / "lib" / "wallpapercolor" / MODULE}) {
        if (fs::exists(file, ec)) return file;
    }
    throw Error(std::format("cannot read image: {} is not installed next to {}: run install.sh again", MODULE,
                            program.string()));
}

/// The decoders, loaded while this exists.
class Decoders {
public:
    Decoders() {
        fs::path file = module_file();
        // RTLD_NOW: a missing symbol fails here. RTLD_LOCAL: its symbols stay its own.
        handle_ = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle_) throw Error(std::format("cannot read image: cannot load {}: {}", file.string(), dlerror()));
        auto entry = reinterpret_cast<const PictureDecoder* (*)()>(dlsym(handle_, "wpc_picture_decoder"));
        decoder_ = entry ? entry() : nullptr;
        if (!decoder_ || std::strcmp(decoder_->built_for, WPC_BUILD_ID) != 0) {
            dlclose(handle_);
            throw Error(std::format("cannot read image: {} {}: run install.sh again", file.string(),
                                    decoder_ ? "is from another build" : "has no picture decoders"));
        }
    }
    ~Decoders() { dlclose(handle_); }
    Decoders(const Decoders&) = delete;
    Decoders& operator=(const Decoders&) = delete;

    const PictureDecoder& operator*() const { return *decoder_; }

private:
    void* handle_ = nullptr;
    const PictureDecoder* decoder_ = nullptr;
};

}  // namespace

Picture PictureFiles::read(const fs::path& path, uint32_t min_side) const {
    Decoders decoders;  // unloaded again when the picture is read
    DecodedPicture decoded;
    try {
        decoded = (*decoders).read(path, min_side);
    } catch (const Error& e) {
        // An error thrown in the module refers to its code: thrown again from here, before the
        // module is unloaded.
        throw Error(e.what());
    }
    return Picture{decoded.width, decoded.height, std::move(decoded.rgb8), std::move(decoded.rgb16)};
}

std::optional<int64_t> PictureFiles::modified_time(const fs::path& path) const {
    std::error_code ec;
    fs::file_time_type t = fs::last_write_time(path, ec);
    if (ec) return std::nullopt;
    return static_cast<int64_t>(t.time_since_epoch().count());
}

}  // namespace wpc
