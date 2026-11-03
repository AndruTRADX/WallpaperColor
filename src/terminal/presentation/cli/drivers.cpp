// `drivers`, `drivers on|off <components…>`, and `config`.

#include <cstdlib>
#include <format>

#include "terminal/application/features/show_settings/show_settings.hpp"
#include "terminal/presentation/cli/commands.hpp"
#include "terminal/presentation/cli/output.hpp"

namespace wpc {

int run_drivers(TerminalParts& parts, const Config& config, SettingsEditor& editor, const std::string& action,
                const std::vector<std::string>& names) {
    if (!action.empty()) {
        choose_components(editor, parts.format, names, action == "on");
        out(std::format("{}: {}", action, join(names, ", ")));
        return EXIT_SUCCESS;
    }
    ComponentList list = list_components(parts.drivers, config);
    for (const std::string& p : list.problems) out("problem: " + p);
    for (const ComponentChoice& c : list.choices) {
        out(std::format("[{}] {:<14} {}", c.chosen ? "x" : " ", component_label(c.component),
                        c.drivers.empty() ? "no driver installed" : join(c.drivers, ", ")));
    }
    out("modules: " + list.location);
    return EXIT_SUCCESS;
}

int run_config(TerminalParts& parts, const Config& config) {
    out(show_settings(parts.format, config));
    return EXIT_SUCCESS;
}

}  // namespace wpc
