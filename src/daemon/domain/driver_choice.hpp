// Which drivers the chosen components need. Pure: loading them is infrastructure's job.

#pragma once

#include <vector>

#include "drivers/sdk/hardware.hpp"
#include "shared/domain/settings/settings.hpp"

namespace wpc {

/// Whether a driver that paints `paints` is needed: it paints at least one chosen component.
bool driver_needed(const std::vector<Component>& paints, const ComponentSettings& chosen);

/// The chosen components that no installed driver paints (`installed`: what each one paints).
std::vector<Component> unpainted(const std::vector<std::vector<Component>>& installed, const ComponentSettings& chosen);

}  // namespace wpc
