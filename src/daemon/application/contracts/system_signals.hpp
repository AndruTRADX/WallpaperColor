// What the service needs to hear when the hardware may have lost or regained its colours.

#pragma once

#include <filesystem>
#include <functional>

namespace wpc {

class SystemSignals {
public:
    virtual ~SystemSignals() = default;
    /// Calls `woke` (on the service's loop) when the computer wakes from sleep: devices may have
    /// lost their colours. Throws wpc::Error.
    virtual void listen(std::function<void()> woke) = 0;
    /// Where the kernel adds a device's file when one is plugged in.
    virtual std::filesystem::path device_folder() = 0;
};

}  // namespace wpc
