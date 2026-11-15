// The real RazerKeyboard: the Huntsman Mini through /dev/hidraw*, no other program needed (and
// no openrazer). Your user needs access to it: udev/70-wallpapercolor.rules gives it to whoever
// is logged in. What the reports say: protocol.hpp.

#pragma once

#include <span>

#include "drivers/razer/driver.hpp"
#include "drivers/razer/protocol.hpp"
#include "utils/fd.hpp"

namespace wpc::razer {

class HidKeyboard final : public RazerKeyboard {
public:
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
