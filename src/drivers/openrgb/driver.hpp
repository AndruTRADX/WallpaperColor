// The openrgb driver: every device of an OpenRGB SDK server (OpenLinkHub's, or OpenRGB's own);
// on a Corsair setup, the RAM, the pump and the fans.
//
// OpenRgbServer is the port to the server (client.hpp is the real one over TCP; the tests talk
// to a fake server). ServerDriver is the Driver the programs see: it connects when needed,
// drops the connection on a failure (the next devices() connects again) and sends a whole
// controller in one message when a frame has all its zones.

#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "drivers/openrgb/protocol.hpp"
#include "drivers/sdk/driver.hpp"

namespace wpc::openrgb {

/// What the driver needs from an OpenRGB SDK server.
class OpenRgbServer {
public:
    virtual ~OpenRgbServer() = default;
    /// Connects to "host:port" and announces `client_name`. Throws wpc::Error.
    virtual void open(std::string_view address, std::string_view client_name) = 0;
    /// Drops the connection (made again by the next open()).
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    /// Every controller, with what its LEDs show now. Throws wpc::Error.
    virtual std::vector<Controller> controllers() = 0;
    /// Sets every LED of controller `index`, in LED order. Throws wpc::Error.
    virtual void set_leds(uint32_t index, std::span<const Rgb> colors) = 0;
    /// Sets the LEDs of zone `zone` of controller `index`. Throws wpc::Error.
    virtual void set_zone_leds(uint32_t index, uint32_t zone, std::span<const Rgb> colors) = 0;
};

/// What it is loaded for. Motherboard and ARGB headers are left to the drivers that talk to the
/// board directly (fusion2): choosing only those doesn't load this one.
inline constexpr Component COMPONENTS[] = {Component::Fans, Component::Pump,  Component::Ram,  Component::Keyboard,
                                           Component::Gpu,  Component::Strip, Component::Other};

class ServerDriver final : public Driver {
public:
    /// `server`: the server's port (module.cpp gives the real client).
    ServerDriver(ClientSettings settings, std::unique_ptr<OpenRgbServer> server)
        : settings_(std::move(settings)), server_(std::move(server)) {}

    /// Connects first if there is no connection. Throws wpc::Error naming the server.
    std::vector<Device> devices() override;
    void show(size_t device, std::span<const ZoneFrame> zones) override;

private:
    ClientSettings settings_;
    std::unique_ptr<OpenRgbServer> server_;
    /// From the last devices(): what show()'s device numbers mean, and how many zones each
    /// has as a device (classify()), so a frame needn't ask again.
    std::vector<Controller> controllers_;
    std::vector<size_t> zone_counts_;
    /// A frame's colours when its zones are not one block in memory (kept: no allocation per frame).
    std::vector<Rgb> joined_;
};

}  // namespace wpc::openrgb
