// Minimal OpenRGB SDK client.
//
// Speaks the subset of the OpenRGB SDK protocol (up to version 4) needed to list controllers
// and set their LEDs. Used against OpenLinkHub's built-in SDK server (port 6743), which only
// exposes devices whose "OpenRGB Integration" toggle is on; it also works with OpenRGB itself.
//
// Wire format: every packet is a 16-byte header ("ORGB", device index, packet id, payload
// size; little endian) followed by the payload. Strings are a u16 length (NUL included) + bytes.

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/color.hpp"
#include "core/paint.hpp"
#include "util/socket.hpp"

namespace wpc::openrgb {

/// Highest protocol version this client understands.
inline constexpr uint32_t PROTOCOL_VERSION = 4;

/// OpenRGB device type of RAM modules.
inline constexpr int32_t DEVICE_TYPE_DRAM = 1;

namespace packet {
inline constexpr uint32_t REQUEST_CONTROLLER_COUNT = 0;
inline constexpr uint32_t REQUEST_CONTROLLER_DATA = 1;
inline constexpr uint32_t REQUEST_PROTOCOL_VERSION = 40;
inline constexpr uint32_t SET_CLIENT_NAME = 50;
inline constexpr uint32_t DEVICE_LIST_UPDATED = 100;
inline constexpr uint32_t UPDATE_LEDS = 1050;
}  // namespace packet

struct Zone {
    std::string name;
    int32_t kind = 0;
    uint32_t leds = 0;
};

struct Controller {
    /// Index used to address the controller in this session (it can change when devices change).
    uint32_t index = 0;
    int32_t kind = 0;
    std::string name;
    std::string vendor;
    std::string description;
    std::string version;
    std::string serial;
    std::string location;
    std::vector<Zone> zones;
    /// Current colour of every LED, in LED order.
    std::vector<Rgb> colors;

    size_t led_count() const { return colors.size(); }

    /// Colours for every LED, zone by zone (see Painter). RAM is painted top to bottom, the
    /// rest as rings: fans, pumps and strips tend to be loops.
    std::vector<Rgb> paint(Painter& painter) const;
};

/// Parses a controller description as sent for protocol `proto` (0-4). Throws wpc::Error.
Controller parse_controller(std::span<const uint8_t> data, uint32_t index, uint32_t proto);

class Client {
public:
    /// Connects to "host:port", announces `client_name` and negotiates the protocol version.
    static Client connect(std::string_view address, std::string_view client_name);

    /// Protocol version agreed with the server.
    uint32_t protocol() const { return protocol_; }

    uint32_t controller_count();
    Controller controller(uint32_t index);
    std::vector<Controller> controllers();

    /// Sets every LED of controller `index`, in LED order. `colors` must not be longer than
    /// the controller's LED count.
    void set_leds(uint32_t index, const std::vector<Rgb>& colors);

private:
    explicit Client(Fd socket) : socket_(std::move(socket)) {}

    void send(uint32_t device, uint32_t id, std::span<const uint8_t> payload);
    /// Reads packets until one with id `want` arrives; device-list notifications are skipped.
    std::vector<uint8_t> receive(uint32_t want);

    Fd socket_;
    uint32_t protocol_ = 0;
};

}  // namespace wpc::openrgb
