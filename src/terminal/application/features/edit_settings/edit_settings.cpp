#include "terminal/application/features/edit_settings/edit_settings.hpp"

#include "terminal/application/features/load_settings/load_settings.hpp"

#include <algorithm>
#include <cmath>

namespace wpc {

namespace {

// In the order of the UI's labels (presentation/tui/home_page.cpp).
const NumberSetting NUMBERS[] = {
    {"led", "fade", 0.1, 0.0, 1.5, 1, [](const Config& c) { return c.led.fade; }},
    {"extract", "min_saturation", 0.05, 0.2, 1.0, 2, [](const Config& c) { return c.extract.min_saturation; }},
    {"extract", "pastel_gain", 0.25, 1.0, 5.0, 2, [](const Config& c) { return c.extract.pastel_gain; }},
    {"extract", "skin_weight", 0.05, 0.0, 1.0, 2, [](const Config& c) { return c.extract.skin_weight; }},
    {"extract", "min_chroma", 0.01, 0.0, 0.5, 2, [](const Config& c) { return c.extract.min_chroma; }},
    {"extract", "min_share", 0.001, 0.0, 0.05, 3, [](const Config& c) { return c.extract.min_share; }},
    {"extract", "palette_size", 1, 2, 32, -1, [](const Config& c) { return static_cast<double>(c.extract.palette_size); }},
    {"extract", "sample_size", 16, 16, 512, -1, [](const Config& c) { return static_cast<double>(c.extract.sample_size); }},
};

}  // namespace

std::span<const NumberSetting> number_settings() {
    return NUMBERS;
}

bool cycle_mode(SettingsEditor& settings, SettingsFormat& format, int direction) {
    int next = (static_cast<int>(edited_settings(format, settings).led.mode) + direction + 3) % 3;
    return settings.set_mode(static_cast<Mode>(next));
}

bool step_number(SettingsEditor& settings, SettingsFormat& format, size_t index, int direction) {
    const NumberSetting& n = NUMBERS[index];
    double v = std::clamp(n.get(edited_settings(format, settings)) + n.step * direction, n.min, n.max);
    return (n.decimals < 0) ? settings.set_integer(n.table, n.key, std::lround(v))
                            : settings.set_float(n.table, n.key, v, n.decimals);
}

bool step_gain(SettingsEditor& settings, SettingsFormat& format, size_t entry, int channel, int direction) {
    double v = edited_settings(format, settings).calibration.entries.at(entry).gain[channel] + 0.05 * direction;
    return settings.set_gain(entry, channel, v);
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

bool add_calibration(SettingsEditor& settings, const CalibrationTarget& target) {
    std::optional<std::string_view> zone;
    if (target.zone) zone = *target.zone;
    return settings.add_calibration(target.device, zone);
}

bool remove_calibration(SettingsEditor& settings, size_t entry) {
    return settings.remove_calibration(entry);
}

}  // namespace wpc
