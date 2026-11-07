// Service: systemd-logind over the system D-Bus. `PrepareForSleep(false)` is sent when the
// computer wakes up; plugged-in devices get their files in /dev. The signal is read in the
// service's loop (no thread of its own).

#pragma once

#include <memory>
#include <optional>

#include "daemon/application/contracts/system_signals.hpp"
#include "utils/event_loop.hpp"

namespace sdbus {
class IConnection;
}  // namespace sdbus

namespace wpc {

class Logind final : public SystemSignals {
public:
    /// The signal will be heard in `loop`.
    explicit Logind(EventLoop& loop);
    ~Logind() override;

    /// Connects to the system bus. Throws wpc::Error.
    void listen(std::function<void()> woke) override;
    std::filesystem::path device_folder() override { return "/dev"; }

private:
    EventLoop& loop_;
    std::unique_ptr<sdbus::IConnection> connection_;
    std::optional<BusFds> bus_fds_;  // while listening
};

}  // namespace wpc
