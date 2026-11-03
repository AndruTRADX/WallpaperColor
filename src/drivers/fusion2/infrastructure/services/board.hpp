// Service: Gigabyte RGB Fusion 2 (the FusionBoard contract), the RGB controller on Gigabyte
// motherboards (an ITE IT5701/IT5702 or IT8297 chip, a USB HID device). We talk to it directly through /dev/hidraw*, no other program
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

#include "drivers/fusion2/application/contracts/fusion_board.hpp"
#include "drivers/fusion2/domain/settings.hpp"
#include "utils/socket.hpp"

namespace wpc::fusion2 {

class Board final : public FusionBoard {
public:
    /// Closed until open().
    Board() = default;

    /// Finds the controller and opens it; nullopt if this computer has none.
    /// Throws wpc::Error if it is there but cannot be used (no permission).
    static std::optional<Board> find(const BoardSettings& settings);

    bool open(const BoardSettings& settings) override;
    void close() override { *this = Board(); }
    bool is_open() const override { return fd_.valid(); }

    /// Shows `colour` on every single-colour zone. Throws wpc::Error.
    void set_onboard(Rgb colour) override;

    /// Shows `colours` on the LEDs of ARGB header `header`, in order. Throws wpc::Error.
    void set_header(int header, const std::vector<Rgb>& colours) override;

private:
    using Report = std::array<uint8_t, 64>;

    Board(Fd fd, const BoardSettings& settings);
    void send(const Report& report);
    Report receive();
    /// A report with just a command and up to two arguments.
    void command(uint8_t command, uint8_t a = 0, uint8_t b = 0);

    Fd fd_;
    /// Where red, green and blue go in each LED's three bytes, e.g. {1, 0, 2} for "GRB".
    std::array<int, 3> order_{0, 1, 2};
    std::array<std::vector<uint32_t>, HEADERS> header_fans_;
    /// LEDs on each header (the sum of its fans).
    uint32_t header_leds(int header) const;
};

}  // namespace wpc::fusion2
