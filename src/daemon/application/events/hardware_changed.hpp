// Event: the hardware may have lost or regained its colours, with no new wallpaper. Heard from:
// - the computer waking from sleep (devices may come back with their own colours);
// - a device's file added (something plugged in again: a keyboard, a USB controller).
// The service paints again, so a device that comes back doesn't wait for the next wallpaper.

#pragma once

#include "daemon/application/contracts/file_watch.hpp"
#include "daemon/application/contracts/system_signals.hpp"
#include "daemon/application/models/notifications.hpp"

namespace wpc {

class HardwareChangedEvent {
public:
    HardwareChangedEvent(SystemSignals& signals, FileWatch& files, Notify notify)
        : signals_(signals), files_(files), notify_(std::move(notify)) {}

    /// Starts listening. Throws wpc::Error when the device folder can't be watched.
    void start();

private:
    SystemSignals& signals_;
    FileWatch& files_;
    Notify notify_;
};

}  // namespace wpc
