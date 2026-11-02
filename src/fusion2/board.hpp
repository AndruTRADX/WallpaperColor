// Gigabyte RGB Fusion 2: the RGB controller on Gigabyte motherboards (an ITE IT5701/IT5702 or
// IT8297 chip, a USB HID device). We talk to it directly through /dev/hidraw*, no other program
// needed. Your user needs access to it: udev/70-wallpapercolor.rules gives it to whoever is
// logged in.
//
// Every message is a 64-byte HID "feature report" starting with the report id 0xCC. The
// protocol comes from OpenRGB (Controllers/GigabyteRGBFusion2USBController).
//
// Two kinds of lights:
// - the board's own single-colour zones (I/O cover, the 12 V RGB header LED_C…): one colour
//   for all of them, set as a "static" effect;
// - the two ARGB headers (addressable fans or strips): every LED on its own ("direct" mode).
//   Their colour order (GRB for most fans) is applied here, so nothing is written to the chip.
//
// Nothing is ever saved to the board's flash memory: after a reboot it shows whatever the BIOS
// sets until the service paints it again.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/color.hpp"
#include "core/config.hpp"
#include "util/socket.hpp"

namespace wpc::fusion2 {

/// How the board appears in `devices`, the status and the terminal UI.
inline constexpr const char* DEVICE_NAME = "Motherboard";
inline constexpr const char* ONBOARD_ZONE = "Onboard";
/// A header whose fans are unknown is one zone; a known fan is a zone of its own.
inline constexpr const char* HEADER_ZONE = "ARGB header";
inline constexpr const char* FAN_ZONE = "ARGB fan";
/// OpenRGB's device type for motherboards.
inline constexpr int32_t DEVICE_TYPE = 0;
/// ARGB headers on the board.
inline constexpr int HEADERS = 2;

class Board {
public:
    /// Finds the controller and opens it; nullopt if this computer has none.
    /// Throws wpc::Error if it is there but cannot be used (no permission).
    static std::optional<Board> open(const MotherboardConfig& config);

    /// What the controller says it is, e.g. "IT5701-GIGABYTE V7.0.15.0".
    const std::string& product() const { return product_; }
    const std::filesystem::path& path() const { return path_; }

    /// One zone of what is on ARGB header `header` (0 or 1).
    struct Zone {
        const char* name;
        uint32_t leds;
    };
    /// The zones on header `header`: each fan of the config, or one zone of one colour when
    /// its fans are unknown (that colour then goes to every LED the header can drive).
    std::vector<Zone> header_zones(int header) const;

    /// Shows `colour` on every single-colour zone. Throws wpc::Error.
    void set_onboard(Rgb colour);

    /// Shows `colours` on the LEDs of ARGB header `header`, in order. Throws wpc::Error.
    void set_header(int header, const std::vector<Rgb>& colours);

private:
    using Report = std::array<uint8_t, 64>;

    Board(Fd fd, std::filesystem::path path, const MotherboardConfig& config);
    void send(const Report& report);
    Report receive();
    /// A report with just a command and up to two arguments.
    void command(uint8_t command, uint8_t a = 0, uint8_t b = 0);

    Fd fd_;
    std::filesystem::path path_;
    std::string product_;
    /// Where red, green and blue go in each LED's three bytes, e.g. {1, 0, 2} for "GRB".
    std::array<int, 3> order_;
    std::array<std::vector<uint32_t>, HEADERS> header_fans_;
    /// LEDs on each header (the sum of its fans).
    uint32_t header_leds(int header) const;
};

}  // namespace wpc::fusion2
