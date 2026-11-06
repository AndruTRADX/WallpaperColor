// Service: a minimal OpenRGB SDK client (the OpenRgbServer contract).
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

#include "drivers/openrgb/application/contracts/openrgb_server.hpp"
#include "drivers/openrgb/domain/controller.hpp"
#include "utils/socket.hpp"

namespace wpc::openrgb {

/// Highest protocol version this client understands.
inline constexpr uint32_t PROTOCOL_VERSION = 4;

namespace packet {
inline constexpr uint32_t REQUEST_CONTROLLER_COUNT = 0;
inline constexpr uint32_t REQUEST_CONTROLLER_DATA = 1;
inline constexpr uint32_t REQUEST_PROTOCOL_VERSION = 40;
inline constexpr uint32_t SET_CLIENT_NAME = 50;
inline constexpr uint32_t DEVICE_LIST_UPDATED = 100;
inline constexpr uint32_t UPDATE_LEDS = 1050;
inline constexpr uint32_t UPDATE_ZONE_LEDS = 1051;
}  // namespace packet

/// Parses a controller description as sent for protocol `proto` (0-4). Throws wpc::Error.
Controller parse_controller(std::span<const uint8_t> data, uint32_t index, uint32_t proto);

class Client final : public OpenRgbServer {
public:
    /// Not connected until open().
    Client() = default;

    /// Connects to "host:port", announces `client_name` and negotiates the protocol version.
    static Client connect(std::string_view address, std::string_view client_name);

    void open(std::string_view address, std::string_view client_name) override { *this = connect(address, client_name); }
    void close() override { *this = Client(); }
    bool is_open() const override { return socket_.valid(); }

    /// Protocol version agreed with the server.
    uint32_t protocol() const { return protocol_; }

    uint32_t controller_count();
    Controller controller(uint32_t index);
    std::vector<Controller> controllers() override;

    /// Sets every LED of controller `index`, in LED order. `colors` must not be longer than
    /// the controller's LED count.
    void set_leds(uint32_t index, std::span<const Rgb> colors) override;

    /// Sets the LEDs of zone `zone` of controller `index`.
    void set_zone_leds(uint32_t index, uint32_t zone, std::span<const Rgb> colors) override;

private:
    explicit Client(Fd socket) : socket_(std::move(socket)) {}

    void send(uint32_t device, uint32_t id, std::span<const uint8_t> payload);
    /// Starts packet `id` for `device` in out_: its header, for a payload of `payload_size` bytes.
    void start_packet(uint32_t device, uint32_t id, size_t payload_size);
    /// Reads packets until one with id `want` arrives; device-list notifications are skipped.
    std::vector<uint8_t> receive(uint32_t want);

    Fd socket_;
    uint32_t protocol_ = 0;
    /// The packet being sent, kept between packets so that sending allocates nothing.
    std::vector<uint8_t> out_;
};

}  // namespace wpc::openrgb
