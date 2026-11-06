#include "daemon/application/events/hardware_changed.hpp"

#include "utils/error.hpp"
#include "utils/log.hpp"

namespace wpc {

void HardwareChangedEvent::start() {
    try {
        signals_.listen([this] { notify_(HardwareChangedNotice{.woke = true}); });
    } catch (const Error& e) {
        log("not hearing when the computer wakes up: {}", e.what());
    }
    // Any device: which ones a driver paints is the drivers' business; a false alarm costs a
    // repaint with the same colours.
    files_.watch(signals_.device_folder(), FILE_CREATED, [this](const FileChange& c) {
        if (c.created || c.overflow) notify_(HardwareChangedNotice{.woke = false});
    });
}

}  // namespace wpc
