#include "daemon/application/features/pause/pause.hpp"

#include "utils/log.hpp"

namespace wpc {

void pause(ServiceState& state) {
    state.paused = true;
    log("paused");
}

void resume(ServiceState& state) {
    state.paused = false;
    log("resumed");
}

}  // namespace wpc
