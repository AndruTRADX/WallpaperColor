// The driver modules as both programs use them.
//
// DriverHost is the port (the tests give a fake one). ModuleHost implements it with the
// wpc-driver-<name>.so files: it lists them, loads one (dlopen) only while its driver is
// needed, and unloads it (dlclose) when the driver goes. Nothing is compiled at run time.
//
// Modules and programs come from the same build and are installed together, so they share
// ordinary C++ types. Each module carries the WPC_BUILD_ID of its build, and any other is
// refused ("run install.sh again").
//
// The boundary is guarded both ways: every call into a module is made through guarded()
// (driver_host.cpp), so an exception a module throws is copied into one made by the program
// while the module is still loaded. The original was made by the module's code (its vtable
// and destructor are there): once the module is unloaded, reading it would jump into memory
// that is gone.

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "drivers/sdk/driver.hpp"
#include "utils/shared_library.hpp"

namespace wpc {

/// A setting of a driver's table that the Drivers page lets the user choose (DriverOption,
/// copied out of its module): one of a few words. The first choice is the driver's default.
struct ModuleOption {
    std::string key;    // "direction"
    std::string label;  // "Direction"
    std::vector<std::string> choices;
    std::string hint;
};

/// An installed driver, before it is loaded.
struct DriverModule {
    std::string name;          // "fusion2"
    std::string config_table;  // "motherboard": its table in config.toml
    std::vector<Component> components;
    /// The settings the Drivers page offers for it.
    std::vector<ModuleOption> options = {};

    bool paints(Component c) const;
};

class DriverHost {
public:
    virtual ~DriverHost() = default;
    /// Every installed driver, by name; those that can't be used are left out, with the reason
    /// in `problems`.
    virtual std::vector<DriverModule> installed(std::vector<std::string>& problems) = 0;
    /// Loads driver `name` (of the last installed()) and creates it from `settings`, replacing
    /// one loaded before; returns the keys of its table it didn't read (probably typos).
    /// Throws wpc::Error.
    virtual std::vector<std::string> load(const std::string& name, const DriverSettings& settings) = 0;
    /// Takes driver `name` out of memory: the driver, then its module.
    virtual void unload(const std::string& name) = 0;
    /// The devices of loaded driver `name`. Throws wpc::Error.
    virtual std::vector<Device> devices(const std::string& name) = 0;
    /// One frame on device `device` of loaded driver `name`. Throws wpc::Error.
    virtual void show(const std::string& name, size_t device, std::span<const ZoneFrame> zones) = 0;
    /// Where drivers are looked for, for messages.
    virtual std::string location() = 0;
};

class ModuleHost final : public DriverHost {
public:
    /// `folder`: where the modules are (default_driver_folder()).
    explicit ModuleHost(std::filesystem::path folder) : folder_(std::move(folder)) {}

    /// To read what a module paints it is loaded briefly and unloaded at once, only the first
    /// time its file is seen (or after the file changed): later calls answer from what was
    /// read then, without loading anything.
    std::vector<DriverModule> installed(std::vector<std::string>& problems) override;
    std::vector<std::string> load(const std::string& name, const DriverSettings& settings) override;
    void unload(const std::string& name) override;
    std::vector<Device> devices(const std::string& name) override;
    void show(const std::string& name, size_t device, std::span<const ZoneFrame> zones) override;
    std::string location() override;

private:
    /// What a module file said about itself, or why it can't be used.
    struct Read {
        std::filesystem::file_time_type modified;
        uintmax_t size = 0;
        std::optional<DriverModule> module;
        std::string problem;
    };
    /// A driver and the module its code is in. Members go in reverse order: the driver is
    /// destroyed while its code is still loaded, then the module is unloaded.
    struct Loaded {
        SharedLibrary module;
        std::unique_ptr<Driver> driver;
    };
    Driver& loaded(const std::string& name);

    std::filesystem::path folder_;
    std::map<std::filesystem::path, Read> read_;
    std::map<std::string, Loaded> loaded_;
};

/// Where this program's driver modules are: drivers/ next to it (the build folder, the release
/// tarball), else ../lib/wallpapercolor/drivers/ (installed: ~/.local/bin →
/// ~/.local/lib/wallpapercolor/drivers).
std::filesystem::path default_driver_folder();

}  // namespace wpc
