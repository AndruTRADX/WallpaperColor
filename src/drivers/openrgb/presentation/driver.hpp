// The openrgb driver as the programs see it: the module's Driver (drivers/sdk). Every device of
// an OpenRGB SDK server (OpenLinkHub's, or OpenRGB's own); on a Corsair setup, the RAM, the pump
// and the fans. It only runs the module's features.

#pragma once

#include <memory>
#include <vector>

#include "drivers/openrgb/application/features/list_devices/list_devices.hpp"
#include "drivers/openrgb/application/features/show_frame/show_frame.hpp"
#include "drivers/sdk/driver.hpp"

namespace wpc::openrgb {

/// What it is loaded for. Motherboard and ARGB headers are left to the drivers that talk to the
/// board directly (fusion2): choosing only those doesn't load this one.
inline constexpr Component COMPONENTS[] = {Component::Fans,     Component::Pump, Component::Ram, Component::Keyboard,
                                           Component::Gpu,      Component::Strip, Component::Other};

class ServerDriver final : public Driver {
public:
    /// `server`: the server's service (module.cpp gives the real one).
    ServerDriver(ClientSettings settings, std::unique_ptr<OpenRgbServer> server)
        : server_(std::move(server)), state_{std::move(settings), {}} {}

    std::vector<Device> devices() override { return list_devices(*server_, state_); }
    void show(size_t device, std::span<const ZoneFrame> zones) override { show_frame(*server_, state_, device, zones); }

private:
    std::unique_ptr<OpenRgbServer> server_;
    ServerState state_;
};

}  // namespace wpc::openrgb
