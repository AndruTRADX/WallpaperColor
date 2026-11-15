#include "terminal/app/settings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <variant>

#include "settings/settings_file.hpp"
#include "utils/error.hpp"

namespace wpc {

// ---------------------------------------------------------------------------------------------
// Reading, showing, saving

LoadedSettings load_settings(const std::optional<std::filesystem::path>& chosen) {
    LoadedSettings out{chosen ? chosen : default_settings_path(), Config{}};
    if (out.path) out.config = read_settings(*out.path);
    return out;
}

Config edited_settings(const SettingsEditor& editor) {
    try {
        return parse_settings(editor.text());
    } catch (const Error&) {
        return Config{};
    }
}

std::string show_settings(const Config& config) {
    std::string text = write_settings(config);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    return text;
}

SavedSettings save_settings(SettingsEditor& editor, const Config& before) {
    SavedSettings out{edited_settings(editor), std::nullopt, false};
    out.extract_changed = out.config.extract != before.extract;
    try {
        editor.save();
    } catch (const Error& e) {
        out.error = e.what();
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// The terminal UI's settings

namespace {

// In the order of the UI's labels (tui/home_page.cpp).
constexpr NumberSetting NUMBERS[] = {
    {"led", "fade", 0.1, 0.0, ranges::FADE.max, 1, [](const Config& c) { return c.led.fade; }},
    {"extract", "min_saturation", 0.05, 0.2, 1.0, 2, [](const Config& c) { return c.extract.min_saturation; }},
    {"extract", "pastel_gain", 0.25, 1.0, 5.0, 2, [](const Config& c) { return c.extract.pastel_gain; }},
    {"extract", "skin_weight", 0.05, 0.0, 1.0, 2, [](const Config& c) { return c.extract.skin_weight; }},
    {"extract", "min_chroma", 0.01, 0.0, 0.5, 2, [](const Config& c) { return c.extract.min_chroma; }},
    {"extract", "min_share", 0.001, 0.0, 0.05, 3, [](const Config& c) { return c.extract.min_share; }},
    {"extract", "palette_size", 1, 2, 32, -1, [](const Config& c) { return static_cast<double>(c.extract.palette_size); }},
    {"extract", "sample_size", 16, 16, 512, -1, [](const Config& c) { return static_cast<double>(c.extract.sample_size); }},
};

// The UI never writes a value the file can't have.
constexpr bool within(const NumberSetting& n, Range r) {
    return n.min >= r.min && n.max <= r.max;
}
static_assert(within(NUMBERS[0], ranges::FADE) && within(NUMBERS[1], ranges::MIN_SATURATION) &&
              within(NUMBERS[2], ranges::PASTEL_GAIN) && within(NUMBERS[3], ranges::SKIN_WEIGHT) &&
              within(NUMBERS[4], ranges::MIN_CHROMA) && within(NUMBERS[5], ranges::MIN_SHARE) &&
              within(NUMBERS[6], ranges::PALETTE_SIZE) && within(NUMBERS[7], ranges::SAMPLE_SIZE));

}  // namespace

std::span<const NumberSetting> number_settings() {
    return NUMBERS;
}

bool cycle_mode(SettingsEditor& editor, int direction) {
    int next = (static_cast<int>(edited_settings(editor).led.mode) + direction + 3) % 3;
    return editor.set_mode(static_cast<Mode>(next));
}

bool step_number(SettingsEditor& editor, size_t index, int direction) {
    if (index >= std::size(NUMBERS)) return true;  // no such setting: nothing to change
    const NumberSetting& n = NUMBERS[index];
    double v = std::clamp(n.get(edited_settings(editor)) + n.step * direction, n.min, n.max);
    return (n.decimals < 0) ? editor.set_integer(n.table, n.key, std::lround(v))
                            : editor.set_float(n.table, n.key, v, n.decimals);
}

bool step_gain(SettingsEditor& editor, size_t entry, int channel, int direction) {
    Config config = edited_settings(editor);
    if (entry >= config.calibration.entries.size() || channel < 0 || channel > 2) return true;  // nothing to change
    double v = config.calibration.entries[entry].gain[static_cast<size_t>(channel)] + 0.05 * direction;
    return editor.set_gain(entry, channel, v);
}

std::vector<CalibrationTarget> calibration_targets(const std::vector<ipc::DeviceInfo>& devices) {
    std::vector<CalibrationTarget> targets;
    std::vector<std::string> seen;
    for (const ipc::DeviceInfo& d : devices) {
        if (std::find(seen.begin(), seen.end(), d.name) != seen.end()) continue;  // e.g. the second RAM stick
        seen.push_back(d.name);
        targets.push_back({d.name, std::nullopt});
        std::vector<std::string> zones;
        for (const ipc::ZoneInfo& z : d.zones) {
            if (std::find(zones.begin(), zones.end(), z.name) != zones.end()) continue;
            zones.push_back(z.name);
            targets.push_back({d.name, z.name});
        }
    }
    return targets;
}

bool add_calibration(SettingsEditor& editor, const CalibrationTarget& target) {
    std::optional<std::string_view> zone;
    if (target.zone) zone = *target.zone;
    return editor.add_calibration(target.device, zone);
}

bool remove_calibration(SettingsEditor& editor, size_t entry) {
    return editor.remove_calibration(entry);
}

// ---------------------------------------------------------------------------------------------
// What is painted

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

ComponentList list_components(DriverHost& drivers, const Config& config) {
    ComponentList out;
    std::vector<DriverModule> installed = drivers.installed(out.problems);
    out.choices = component_choices(installed, config);
    out.location = drivers.location();
    return out;
}

void choose_components(SettingsEditor& editor, const std::vector<std::string>& names, bool on) {
    if (names.empty()) throw Error("which components? e.g. wallpapercolor drivers off ram");
    std::array<bool, ALL_COMPONENTS.size()> chosen = edited_settings(editor).components.on;
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

bool toggle_component(SettingsEditor& editor, Component component) {
    std::array<bool, ALL_COMPONENTS.size()> chosen = edited_settings(editor).components.on;
    chosen[static_cast<size_t>(component)] = !chosen[static_cast<size_t>(component)];
    return editor.set_components(chosen);
}

// ---------------------------------------------------------------------------------------------
// The drivers' own settings

std::vector<DriverOptionValue> driver_options(const std::vector<DriverModule>& installed, const Config& config) {
    std::vector<DriverOptionValue> out;
    for (const DriverModule& m : installed) {
        DriverSettings table = config.drivers.of(m.config_table);
        const std::map<std::string, DriverSettings::Value>& values = table.values();
        for (const ModuleOption& o : m.options) {
            if (o.choices.empty()) continue;
            std::string value = o.choices[0];
            auto found = values.find(o.key);
            if (found != values.end()) {
                if (const std::string* s = std::get_if<std::string>(&found->second)) value = *s;
            }
            out.push_back(DriverOptionValue{m.name, m.config_table, o, value});
        }
    }
    return out;
}

bool cycle_driver_option(SettingsEditor& editor, const DriverOptionValue& option, int direction) {
    const std::vector<std::string>& choices = option.option.choices;
    if (choices.empty()) return true;
    auto at = std::find(choices.begin(), choices.end(), option.value);
    size_t n = choices.size();
    // A value that isn't one of the choices (a typo in the file) goes to the first one.
    size_t next = 0;
    if (at != choices.end()) next = (static_cast<size_t>(at - choices.begin()) + (direction < 0 ? n - 1 : 1)) % n;
    return editor.set_string(option.table, option.option.key, choices[next]);
}

}  // namespace wpc
