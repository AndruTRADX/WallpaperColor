#include "drivers/host/services/module_catalog.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>

#include <dlfcn.h>

namespace wpc {

namespace fs = std::filesystem;

namespace {

/// A driver and the module its code is in: calls go to the driver, and the module is unloaded
/// only after the driver is destroyed (members are destroyed in reverse order).
class ModuleDriver final : public Driver {
public:
    ModuleDriver(Module module, std::unique_ptr<Driver> driver) : module_(std::move(module)), driver_(std::move(driver)) {}

    std::vector<Device> devices() override { return driver_->devices(); }
    void show(size_t device, std::span<const ZoneFrame> zones) override { driver_->show(device, zones); }

private:
    Module module_;
    std::unique_ptr<Driver> driver_;
};

}  // namespace

Module Module::load(const fs::path& file) {
    // RTLD_NOW: a missing symbol fails here, not in the middle of a fade.
    // RTLD_LOCAL: modules don't see each other's symbols.
    void* handle = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) throw Error(std::format("cannot load {}: {}", file.string(), dlerror()));

    auto entry = reinterpret_cast<const DriverInfo* (*)()>(dlsym(handle, "wpc_driver_info"));
    const DriverInfo* info = entry ? entry() : nullptr;
    if (!info) {
        dlclose(handle);
        throw Error(std::format("{} is not a WallpaperColor driver", file.string()));
    }
    if (std::strcmp(info->built_for, WPC_BUILD_ID) != 0) {
        std::string other = info->built_for;
        dlclose(handle);
        throw Error(std::format("{} is from another build ({}, these programs are {}): run install.sh again",
                                file.string(), other, WPC_BUILD_ID));
    }
    return Module(handle, info);
}

Module::Module(Module&& other) noexcept : handle_(other.handle_), info_(other.info_) {
    other.handle_ = nullptr;
    other.info_ = nullptr;
}

Module::~Module() {
    if (handle_) dlclose(handle_);
}

std::optional<fs::path> ModuleCatalog::folder() {
    std::vector<fs::path> candidates;
    if (const char* dir = std::getenv("WPC_DRIVER_DIR")) candidates.emplace_back(dir);
    std::error_code ec;
    fs::path program = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        candidates.push_back(program.parent_path() / "drivers");
        candidates.push_back(program.parent_path().parent_path() / "lib" / "wallpapercolor" / "drivers");
    }
    for (const fs::path& dir : candidates) {
        if (fs::is_directory(dir, ec)) return dir;
    }
    return std::nullopt;
}

std::string ModuleCatalog::location() const {
    std::optional<fs::path> dir = folder();
    return dir ? dir->string() : "none";
}

std::vector<InstalledModule> ModuleCatalog::installed(std::vector<std::string>& problems) {
    std::vector<InstalledModule> out;
    std::optional<fs::path> dir = folder();
    if (!dir) return out;

    std::vector<fs::path> files;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(*dir, ec)) {
        std::string name = entry.path().filename().string();
        if (name.starts_with("wpc-driver-") && name.ends_with(".so")) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    std::map<fs::path, Read> seen;
    for (const fs::path& file : files) {
        fs::file_time_type modified = fs::last_write_time(file, ec);
        uintmax_t size = fs::file_size(file, ec);
        auto known = read_.find(file);
        Read r;
        if (known != read_.end() && known->second.modified == modified && known->second.size == size) {
            r = known->second;  // the same file: nothing to load
        } else {
            r = Read{modified, size, std::nullopt, {}};
            try {
                Module m = Module::load(file);  // unloaded again at the end of this block
                const DriverInfo& info = m.info();
                r.module = InstalledModule{info.name, info.config_table, {info.components.begin(), info.components.end()}, file, {}};
                for (const DriverOption& o : info.options) {
                    r.module->options.push_back(InstalledOption{o.key, o.label, {o.choices.begin(), o.choices.end()}, o.hint});
                }
            } catch (const Error& e) {
                r.problem = e.what();
            }
        }
        if (r.module) out.push_back(*r.module);
        else problems.push_back(r.problem);
        seen.emplace(file, std::move(r));
    }
    read_ = std::move(seen);  // removed files are forgotten
    return out;
}

Module ModuleCatalog::load(const InstalledModule& module) {
    return Module::load(module.file);
}

LoadedModule ModuleCatalog::create(Module module, const DriverSettings& settings) {
    std::unique_ptr<Driver> driver = module.info().create(settings);
    return LoadedModule{std::make_unique<ModuleDriver>(std::move(module), std::move(driver)), settings.unread()};
}

}  // namespace wpc
