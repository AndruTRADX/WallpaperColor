// Service: systemd-logind over the system D-Bus. `PrepareForSleep(false)` is sent when the
// computer wakes up; plugged-in devices get their files in /dev.

#pragma once

#include <memory>

#include "daemon/application/contracts/system_signals.hpp"

namespace sdbus {
class IConnection;
}  // namespace sdbus

namespace wpc {

class Logind final : public SystemSignals {
public:
    Logind();
    ~Logind() override;

    /// Connects to the system bus. Throws wpc::Error.
    void listen(std::function<void()> woke) override;
    std::filesystem::path device_folder() override { return "/dev"; }

private:
    std::unique_ptr<sdbus::IConnection> connection_;
};

}  // namespace wpc
