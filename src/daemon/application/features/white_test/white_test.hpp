// Feature: the white test. Everything turns white, through the calibrations, so devices can be
// compared while adjusting them; the wallpaper is ignored until it ends.

#pragma once

#include "daemon/application/contracts/clock.hpp"
#include "daemon/application/contracts/external/driver_modules.hpp"
#include "daemon/application/models/service_state.hpp"

namespace wpc {

void start_white_test(ServiceState& state);
void stop_white_test(ServiceState& state);

/// Paints everything white (while the test is on, instead of the wallpaper).
void paint_white(ServiceState& state, DriverModules& drivers, Clock& clock);

}  // namespace wpc
