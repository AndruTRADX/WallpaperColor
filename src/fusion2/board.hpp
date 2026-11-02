// Gigabyte RGB Fusion 2: the RGB controller on Gigabyte motherboards (an ITE IT5702 or IT8297
// chip, a USB HID device). We talk to it directly through /dev/hidraw*, no other program needed.
// Your user needs access to it: udev/70-wallpapercolor.rules gives it to whoever is logged in.
//
// Every message is a 64-byte HID "feature report" starting with the report id 0xCC. The
// protocol comes from OpenRGB (Controllers/GigabyteRGBFusion2USBController).
//
// Only the board's single-colour zones are painted (I/O cover, the 12 V RGB header LED_C…),
// all with the same colour. Nothing is ever saved to the board's flash memory: after a reboot it
// shows whatever the BIOS sets until the service paints it again.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "core/color.hpp"
#include "util/socket.hpp"

namespace wpc::fusion2 {

/// How the board appears in `devices`, the status and the terminal UI.
inline constexpr const char* DEVICE_NAME = "Motherboard";
inline constexpr const char* ZONE_NAME = "Onboard";
/// OpenRGB's device type for motherboards.
inline constexpr int32_t DEVICE_TYPE = 0;

class Board {
public:
    /// Finds the controller and opens it; nullopt if this computer has none.
    /// Throws wpc::Error if it is there but cannot be used (no permission).
    static std::optional<Board> open();

    /// What the controller says it is, e.g. "IT5702-GIGABYTE V3.5.14.0".
    const std::string& product() const { return product_; }
    const std::filesystem::path& path() const { return path_; }

    /// Shows `colour` on every single-colour zone. Throws wpc::Error.
    void set_colour(Rgb colour);

private:
    using Report = std::array<uint8_t, 64>;

    Board(Fd fd, std::filesystem::path path) : fd_(std::move(fd)), path_(std::move(path)) {}
    void send(const Report& report);
    Report receive();
    /// A report with just a command and up to two arguments.
    void command(uint8_t command, uint8_t a = 0, uint8_t b = 0);

    Fd fd_;
    std::filesystem::path path_;
    std::string product_;
};

}  // namespace wpc::fusion2
