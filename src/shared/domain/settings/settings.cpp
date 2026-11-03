#include "shared/domain/settings/settings.hpp"

#include "utils/text.hpp"

namespace wpc {

Component ComponentSettings::component_of(std::string_view device, std::string_view zone, Component guess) const {
    for (const ComponentRule& r : rules) {
        if (contains_ignoring_case(device, r.device) && (!r.zone || contains_ignoring_case(zone, *r.zone))) return r.is;
    }
    return guess;
}

DriverSettings DriverTables::of(const std::string& name) const {
    auto found = tables.find(name);
    return found != tables.end() ? found->second : DriverSettings(name, {});
}

}  // namespace wpc
