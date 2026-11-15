#include "utils/shared_library.hpp"

#include <utility>

#include <dlfcn.h>

#include "utils/error.hpp"

namespace wpc {

SharedLibrary::SharedLibrary(const std::filesystem::path& file) : file_(file) {
    handle_ = ::dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        const char* why = ::dlerror();
        throw Error(cat("cannot load ", file.string(), ": ", why ? why : "unknown error"));
    }
}

SharedLibrary::~SharedLibrary() {
    if (handle_) ::dlclose(handle_);
}

SharedLibrary::SharedLibrary(SharedLibrary&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)), file_(std::move(other.file_)) {}

SharedLibrary& SharedLibrary::operator=(SharedLibrary&& other) noexcept {
    if (this != &other) {
        if (handle_) ::dlclose(handle_);
        handle_ = std::exchange(other.handle_, nullptr);
        file_ = std::move(other.file_);
    }
    return *this;
}

void* SharedLibrary::symbol(const char* name) const {
    return handle_ ? ::dlsym(handle_, name) : nullptr;
}

}  // namespace wpc
