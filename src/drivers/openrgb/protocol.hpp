// The OpenRGB SDK protocol, the part this driver needs (versions 0 to 4), as pure functions:
// what a controller is, decoding a controller's description, what each zone is, and the
// `[openrgb]` settings. Sending and receiving the bytes is client.cpp.
//
// Wire format: every packet is a 16-byte header ("ORGB", device index, packet id, payload
// size; little endian) followed by the payload. Strings are a u16 length (NUL included) + bytes.
// Server data is untrusted: every read is checked against the payload's size.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "drivers/sdk/driver_settings.hpp"
#include "drivers/sdk/hardware.hpp"

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

/// OpenRGB device types (the ones told apart).
namespace device_type {
inline constexpr int32_t MOTHERBOARD = 0;
inline constexpr int32_t DRAM = 1;
inline constexpr int32_t GPU = 2;
inline constexpr int32_t COOLER = 3;
inline constexpr int32_t LED_STRIP = 4;
inline constexpr int32_t KEYBOARD = 5;
}  // namespace device_type

/// A zone as the server describes it.
struct Zone {
    std::string name;
    uint32_t leds = 0;
};

/// An OpenRGB controller as the server describes it: a device with zones and LEDs.
struct Controller {
    /// Index used to address the controller in this session (it can change when devices change).
    uint32_t index = 0;
    int32_t kind = 0;
    std::string name;
    std::vector<Zone> zones;
    /// Current colour of every LED, in LED order.
    std::vector<Rgb> colors;

    size_t led_count() const { return colors.size(); }
};

/// Throws wpc::Error("OpenRGB protocol error: …").
[[noreturn]] void protocol_error(const std::string& message);

/// Parses a controller description as sent for protocol `proto` (0-4). Throws wpc::Error.
Controller parse_controller(std::span<const uint8_t> data, uint32_t index, uint32_t proto);

/// What each zone of a controller is, from its device type and the zone's name:
///   RAM: `ram`, painted top to bottom; a zone with "fan" in its name: `fans`; with "pump":
///   `pump`; any other zone of a cooler (an AIO's head, like "H150i ELITE CAPELLIX XT"): `pump`;
///   motherboards, GPUs, LED strips, keyboards by their device type; the rest `other`.
/// Rings unless the thing is a line (RAM, strips, keyboards). When the zones don't add up to the
/// device's LEDs, the whole device is one zone. The user can correct it with [[component]].
std::vector<wpc::Zone> classify(const Controller& c);

/// The `[openrgb]` table.
struct ClientSettings {
    /// host:port. OpenLinkHub's built-in server listens on 6743 (enableOpenRGBTargetServer);
    /// OpenRGB itself on 6742.
    std::string address = "127.0.0.1:6743";
    /// Name shown to the server for this client.
    std::string client_name = "WallpaperColor";
};

/// Throws wpc::Error for a wrong type.
ClientSettings read_settings(const DriverSettings& table);

}  // namespace wpc::openrgb
