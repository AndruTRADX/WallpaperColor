// Service: a Razer Chroma keyboard (the RazerKeyboard contract), today the Razer Huntsman Mini
// (USB 1532:0257). We talk to it directly through /dev/hidraw*, no other program needed (and
// no openrazer). Your user needs access to it: udev/70-wallpapercolor.rules gives it to whoever
// is logged in.
//
// Every message is a 90-byte HID feature report (report id 0) sent to the keyboard's USB
// interface 2, the one for its lights:
//
//   status, transaction id, remaining packets (2), protocol, data size, command class,
//   command id, 80 bytes of arguments, checksum (XOR of bytes 2-87), reserved
//
// A frame is one "custom frame" report per row of keys (class 0x0F, command 0x03), then the
// "custom" effect (class 0x0F, command 0x02), which shows them. The protocol comes from
// openrazer (driver/razerchromacommon.c) and OpenRGB (Controllers/RazerController).
//
// Nothing is ever saved to the keyboard's memory: unplugged and plugged in again, it shows its
// own effect until the service paints it again.

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "drivers/razer/application/contracts/razer_keyboard.hpp"
#include "utils/socket.hpp"

namespace wpc::razer {

class HidKeyboard final : public RazerKeyboard {
public:
    /// What goes to the device: report id 0, then the 90-byte report.
    using Report = std::array<uint8_t, 91>;

    /// The report that loads row `row`, from column 0 (at most 25 colours: the rest are left
    /// out).
    static Report row_report(uint8_t row, std::span<const Rgb> colours);
    /// The report that shows the rows loaded (the "custom" effect).
    static Report show_report();

    bool open() override;
    void close() override { fd_ = Fd(); }
    bool is_open() const override { return fd_.valid(); }
    bool still_connected() const override;
    void set_row(uint8_t row, std::span<const Rgb> colours) override { send(row_report(row, colours)); }
    void show() override { send(show_report()); }

private:
    void send(const Report& report);

    Fd fd_;
};

}  // namespace wpc::razer
