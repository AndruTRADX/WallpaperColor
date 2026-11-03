#include "terminal/application/features/choose_components/choose_components.hpp"

#include <format>

#include "terminal/application/features/load_settings/load_settings.hpp"
#include "utils/error.hpp"

namespace wpc {

std::vector<ComponentChoice> component_choices(const std::vector<DriverModule>& installed, const Config& config,
                                               const ipc::Status* status) {
    std::vector<ComponentChoice> out;
    for (Component c : ALL_COMPONENTS) {
        ComponentChoice choice{c, config.components.wants(c), {}, false, std::nullopt};
        for (const DriverModule& m : installed) {
            if (!m.paints(c)) continue;
            choice.drivers.push_back(m.name);
            if (!status) continue;
            for (const ipc::DriverState& d : status->drivers) {
                if (d.name != m.name) continue;
                if (d.error) choice.error = d.error;
                else if (d.loaded && choice.chosen) choice.loaded = true;
            }
        }
        out.push_back(std::move(choice));
    }
    return out;
}

ComponentList list_components(DriverModules& drivers, const Config& config) {
    ComponentList out;
    std::vector<DriverModule> installed = drivers.installed(out.problems);
    out.choices = component_choices(installed, config);
    out.location = drivers.location();
    return out;
}

void choose_components(SettingsEditor& editor, SettingsFormat& format, const std::vector<std::string>& names, bool on) {
    if (names.empty()) throw Error("which components? e.g. wallpapercolor drivers off ram");
    std::array<bool, ALL_COMPONENTS.size()> chosen = edited_settings(format, editor).components.on;
    for (const std::string& name : names) {
        std::optional<Component> c = component_from_name(name);
        if (!c) {
            std::string all;
            for (Component k : ALL_COMPONENTS) all += (all.empty() ? "" : ", ") + std::string(component_name(k));
            throw Error(std::format("unknown component `{}`: one of {}", name, all));
        }
        chosen[static_cast<size_t>(*c)] = on;
    }
    if (!editor.set_components(chosen)) throw Error("[components] has a layout this cannot edit: change it in the file");
    editor.save();
}

bool toggle_component(SettingsEditor& editor, SettingsFormat& format, Component component) {
    std::array<bool, ALL_COMPONENTS.size()> chosen = edited_settings(format, editor).components.on;
    chosen[static_cast<size_t>(component)] = !chosen[static_cast<size_t>(component)];
    return editor.set_components(chosen);
}

}  // namespace wpc
