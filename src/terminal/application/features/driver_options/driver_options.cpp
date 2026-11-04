#include "terminal/application/features/driver_options/driver_options.hpp"

#include <algorithm>
#include <variant>

namespace wpc {

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
