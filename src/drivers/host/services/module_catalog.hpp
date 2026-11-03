// Service: how a program finds and loads driver modules: the wpc-driver-<name>.so files, with dlopen.
// Both programs reach it through their contracts/external/driver_modules.hpp.
//
// Loading a module maps its already-compiled code into the program; unloading it (dlclose)
// takes it out again. Nothing is compiled at run time.
//
// Modules and programs come from the same build and are installed together, so they share
// ordinary C++ types (strings, vectors, exceptions). Each module carries the WPC_BUILD_ID of
// its build, and any other is refused ("run install.sh again").
//
// Where they are, the first folder that exists:
//   $WPC_DRIVER_DIR                     (tests)
//   drivers/ next to the program        (the build folder, the release tarball)
//   ../lib/wallpapercolor/drivers/      (installed: ~/.local/bin → ~/.local/lib/wallpapercolor/drivers)

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <memory>

#include "drivers/sdk/driver.hpp"

namespace wpc {

/// One loaded module file. Move-only; unloaded when destroyed.
class Module {
public:
    /// Loads `file` and checks it. Throws wpc::Error.
    static Module load(const std::filesystem::path& file);

    const DriverInfo& info() const { return *info_; }

    Module(Module&& other) noexcept;
    Module& operator=(Module&&) = delete;
    Module(const Module&) = delete;
    ~Module();

private:
    Module(void* handle, const DriverInfo* info) : handle_(handle), info_(info) {}
    void* handle_ = nullptr;
    const DriverInfo* info_ = nullptr;
};

/// An installed module, before it is loaded.
struct InstalledModule {
    std::string name;          // "fusion2"
    std::string config_table;  // "motherboard": its table in config.toml
    std::vector<Component> components;
    std::filesystem::path file;
};

/// A driver created from its module.
struct LoadedModule {
    /// Keeps its module in memory while it lives; destroying it unloads the module.
    std::unique_ptr<Driver> driver;
    /// Keys of its config table it didn't read (probably typos).
    std::vector<std::string> unread_settings;
};

class ModuleCatalog {
public:
    /// Every installed module, by name. To read what it paints, a module is loaded briefly and
    /// unloaded at once, only the first time its file is seen (or after the file changed): later
    /// calls answer from what was read then, without loading anything. Modules that can't be
    /// used are left out, with the reason in `problems`.
    std::vector<InstalledModule> installed(std::vector<std::string>& problems);

    /// Loads the file of `module` into memory and checks it. Throws wpc::Error.
    Module load(const InstalledModule& module);

    /// Creates the driver of a loaded `module` from `settings`; the driver keeps the module in
    /// memory and unloads it when destroyed. Throws wpc::Error.
    LoadedModule create(Module module, const DriverSettings& settings);

    /// Where modules are looked for, for messages ("none" if nowhere).
    std::string location() const;

    /// The folder modules are loaded from; nullopt if there is none.
    static std::optional<std::filesystem::path> folder();

private:
    /// What a module file said about itself, or why it can't be used.
    struct Read {
        std::filesystem::file_time_type modified;
        uintmax_t size = 0;
        std::optional<InstalledModule> module;
        std::string problem;
    };
    std::map<std::filesystem::path, Read> read_;
};

}  // namespace wpc
