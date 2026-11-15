// SharedLibrary: a shared object loaded with dlopen() while the object lives, unloaded
// (dlclose) when it goes (RAII). Move-only: one owner per load.
//
// What the library hands out (functions, data, objects whose code is in it, exceptions it
// threw) must not outlive it: its code is unmapped when it is unloaded.

#pragma once

#include <filesystem>

namespace wpc {

class SharedLibrary {
public:
    /// Loads `file`. RTLD_NOW: a missing symbol fails here, not in the middle of a call.
    /// RTLD_LOCAL: its symbols stay its own. Throws wpc::Error.
    explicit SharedLibrary(const std::filesystem::path& file);
    ~SharedLibrary();
    SharedLibrary(SharedLibrary&& other) noexcept;
    SharedLibrary& operator=(SharedLibrary&& other) noexcept;
    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;

    /// The exported function `name`, as a `Function` (a function pointer type); nullptr if the
    /// library has none.
    template <typename Function>
    Function function(const char* name) const {
        return reinterpret_cast<Function>(symbol(name));
    }

    const std::filesystem::path& file() const { return file_; }

private:
    void* symbol(const char* name) const;

    void* handle_ = nullptr;
    std::filesystem::path file_;
};

}  // namespace wpc
