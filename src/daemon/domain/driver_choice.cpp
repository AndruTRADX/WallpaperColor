#include "daemon/domain/driver_choice.hpp"

#include <algorithm>

namespace wpc {

bool driver_needed(const std::vector<Component>& paints, const ComponentSettings& chosen) {
    return std::any_of(paints.begin(), paints.end(), [&](Component c) { return chosen.wants(c); });
}

std::vector<Component> unpainted(const std::vector<std::vector<Component>>& installed, const ComponentSettings& chosen) {
    std::vector<Component> out;
    for (Component c : ALL_COMPONENTS) {
        if (!chosen.wants(c)) continue;
        bool painted = std::any_of(installed.begin(), installed.end(), [&](const std::vector<Component>& paints) {
            return std::find(paints.begin(), paints.end(), c) != paints.end();
        });
        if (!painted) out.push_back(c);
    }
    return out;
}

}  // namespace wpc
