#include "drivers/host/driver_host.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <new>

#include "utils/error.hpp"
#include "utils/files.hpp"

namespace wpc {

namespace fs = std::filesystem;

namespace {

/// Runs `call`, which goes into the code of module `module`. Whatever it throws is thrown again
/// as a wpc::Error made here, while the module is still loaded: the original exception's code
/// is the module's, and the module may be unloaded before the error is read.
template <typename Call>
auto guarded(const std::string& module, Call&& call) -> decltype(call()) {
    try {
        return std::forward<Call>(call)();
    } catch (const Error& e) {
        throw Error(e.what());
    } catch (const std::bad_alloc&) {
        throw Error(cat(module, ": out of memory"));
    } catch (const std::exception& e) {
        throw Error(cat(module, ": ", e.what()));
    } catch (...) {
        throw Error(cat(module, ": failed with an unknown error"));
    }
}

/// The driver a loaded module describes, after checking it comes from this build.
const DriverInfo& driver_info(const SharedLibrary& module) {
    auto entry = module.function<const DriverInfo* (*)()>("wpc_driver_info");
    const DriverInfo* info = entry ? entry() : nullptr;
    if (!info) throw Error(std::format("{} is not a WallpaperColor driver", module.file().string()));
    // `built_for` is the first member in every build: safe to read before the rest.
    if (std::strcmp(info->built_for, WPC_BUILD_ID) != 0) {
        throw Error(std::format("{} is from another build ({}, these programs are {}): run install.sh again",
                                module.file().string(), info->built_for, WPC_BUILD_ID));
    }
    return *info;
}

/// What the Drivers page needs of a module, copied out of it (it is unloaded right after).
DriverModule describe(const DriverInfo& info) {
    DriverModule m{info.name, info.config_table, {info.components.begin(), info.components.end()}, {}};
    for (const DriverOption& o : info.options) {
        m.options.push_back(ModuleOption{o.key, o.label, {o.choices.begin(), o.choices.end()}, o.hint});
    }
    return m;
}

}  // namespace

bool DriverModule::paints(Component c) const {
    return std::find(components.begin(), components.end(), c) != components.end();
}

fs::path default_driver_folder() {
    std::optional<fs::path> program = program_folder();
    if (!program) return fs::path("drivers");
    fs::path next_to = *program / "drivers";
    std::error_code ec;
    if (fs::is_directory(next_to, ec)) return next_to;
    return program->parent_path() / "lib" / "wallpapercolor" / "drivers";
}

std::string ModuleHost::location() {
    std::error_code ec;
    return fs::is_directory(folder_, ec) ? folder_.string() : "none";
}

std::vector<DriverModule> ModuleHost::installed(std::vector<std::string>& problems) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder_, ec)) {
        std::string name = entry.path().filename().string();
        if (name.starts_with("wpc-driver-") && name.ends_with(".so")) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    std::vector<DriverModule> out;
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
                SharedLibrary module(file);  // unloaded again at the end of this block
                r.module = describe(driver_info(module));
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

std::vector<std::string> ModuleHost::load(const std::string& name, const DriverSettings& settings) {
    auto found = std::find_if(read_.begin(), read_.end(),
                              [&](const auto& entry) { return entry.second.module && entry.second.module->name == name; });
    if (found == read_.end()) throw Error(std::format("driver {} is not installed", name));

    SharedLibrary module(found->first);
    const DriverInfo& info = driver_info(module);
    std::unique_ptr<Driver> driver = guarded(name, [&] { return info.create(settings); });
    if (!driver) throw Error(std::format("{}: the module made no driver", name));
    std::vector<std::string> unread = settings.unread();
    loaded_.erase(name);
    loaded_.emplace(name, Loaded{std::move(module), std::move(driver)});
    return unread;
}

void ModuleHost::unload(const std::string& name) {
    loaded_.erase(name);
}

Driver& ModuleHost::loaded(const std::string& name) {
    auto it = loaded_.find(name);
    if (it == loaded_.end()) throw Error(std::format("driver {} is not loaded", name));
    return *it->second.driver;
}

std::vector<Device> ModuleHost::devices(const std::string& name) {
    Driver& driver = loaded(name);
    return guarded(name, [&] { return driver.devices(); });
}

void ModuleHost::show(const std::string& name, size_t device, std::span<const ZoneFrame> zones) {
    Driver& driver = loaded(name);
    guarded(name, [&] { driver.show(device, zones); });
}

}  // namespace wpc
