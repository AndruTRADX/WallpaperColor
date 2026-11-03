#include "daemon/application/features/apply_settings/apply_settings.hpp"

#include <algorithm>

#include "daemon/domain/driver_choice.hpp"
#include "utils/error.hpp"
#include "utils/log.hpp"

namespace wpc {

namespace {

const DriverModule* find_module(const std::vector<DriverModule>& installed, const std::string& name) {
    auto it = std::find_if(installed.begin(), installed.end(), [&](const DriverModule& m) { return m.name == name; });
    return it == installed.end() ? nullptr : &*it;
}

/// Says what in the settings no installed driver uses.
void log_unused(const std::vector<DriverModule>& installed, const Config& config) {
    for (const auto& [table, settings] : config.drivers.tables) {
        bool known = std::any_of(installed.begin(), installed.end(), [&](const DriverModule& m) { return m.config_table == table; });
        if (!known) log("config: no installed driver uses [{}]", table);
    }
    if (!config.components.chosen) return;
    std::vector<std::vector<Component>> paints;
    for (const DriverModule& m : installed) paints.push_back(m.components);
    std::string orphans;
    for (Component c : unpainted(paints, config.components)) orphans += (orphans.empty() ? "" : ", ") + std::string(component_name(c));
    if (!orphans.empty()) log("no installed driver paints {}", orphans);
}

}  // namespace

void apply_settings(ServiceState& state, SettingsSource& settings, DriverModules& drivers, Memory& memory,
                    bool first) {
    try {
        state.config = settings.load();
    } catch (const Error& e) {
        log("{}; using the defaults", e.what());
        state.config = Config{};
    }

    DriversState& d = state.drivers;
    const ComponentSettings& components = state.config.components;
    std::vector<std::string> problems;
    d.installed = drivers.installed(problems);
    for (const std::string& p : problems) log("{}", p);
    if (d.installed.empty()) log("no driver modules found (looked in {})", drivers.location());
    log_unused(d.installed, state.config);

    // Unload what isn't needed, or whose settings changed (it is loaded again below).
    bool unloaded = false;
    for (auto it = d.loaded.begin(); it != d.loaded.end();) {
        const DriverModule* m = find_module(d.installed, it->first);
        bool keep = m && driver_needed(m->components, components) &&
                    state.config.drivers.of(m->config_table) == it->second;
        if (keep) {
            ++it;
            continue;
        }
        drivers.unload(it->first);
        log("driver {}: unloaded", it->first);
        d.errors.erase(it->first);
        it = d.loaded.erase(it);
        unloaded = true;
    }
    if (unloaded) memory.release();  // give the driver's memory back to the system

    for (const DriverModule& m : d.installed) {
        if (!driver_needed(m.components, components) || d.loaded.contains(m.name)) continue;
        DriverSettings table = state.config.drivers.of(m.config_table);
        try {
            for (const std::string& key : drivers.load(m.name, table)) {
                log("config: [{}] {}: unknown setting", m.config_table, key);
            }
            d.loaded.emplace(m.name, table);
            std::string chosen;
            for (Component c : m.components) {
                if (components.wants(c)) chosen += (chosen.empty() ? "" : ", ") + std::string(component_name(c));
            }
            log("driver {}: loaded for {}", m.name, chosen);
        } catch (const Error& e) {
            d.errors[m.name] = e.what();
            log("driver {}: {}", m.name, e.what());
        }
    }
    d.shown.clear();  // devices may have changed: fades start from what they report

    if (first) {
        log("wallpapercolord {}: mode {}", WPC_VERSION, mode_name(state.config.led.mode));
    } else {
        log("config reloaded: mode {}", mode_name(state.config.led.mode));
    }
}

}  // namespace wpc
