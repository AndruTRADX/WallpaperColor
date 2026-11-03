#include "daemon/application/features/white_test/white_test.hpp"

#include "daemon/application/features/paint_wallpaper/paint_wallpaper.hpp"
#include "utils/log.hpp"

namespace wpc {

void start_white_test(ServiceState& state) {
    state.testing = true;
    log("white test on");
}

void stop_white_test(ServiceState& state) {
    state.testing = false;
    log("white test off");
}

void paint_white(ServiceState& state, DriverModules& drivers, Clock& clock) {
    state.last_painted.reset();  // the wallpaper's colours are painted again when the test ends
    paint_colours(state, drivers, clock, {WHITE}, Mode::Single);
}

}  // namespace wpc
