// What the openrgb driver needs from an OpenRGB SDK server.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "drivers/openrgb/domain/controller.hpp"

namespace wpc::openrgb {

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

}  // namespace wpc::openrgb
