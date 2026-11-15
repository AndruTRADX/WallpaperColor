#include "drivers/razer/driver.hpp"

#include "utils/error.hpp"

namespace wpc::razer {

std::vector<Device> KeyboardDriver::devices() {
    if (keyboard_->is_open() && !keyboard_->still_connected()) keyboard_->close();  // unplugged since
    if (!keyboard_->is_open() && !keyboard_->open()) return {};  // not plugged in is not an error
    return {keyboard_device()};
}

void KeyboardDriver::show(size_t, std::span<const ZoneFrame> zones) {
    if (!keyboard_->is_open()) throw Error("Razer keyboard: not open");
    try {
        for (const ZoneFrame& z : zones) {  // the keyboard has one zone
            KeyGrid grid = place_keys(settings_, z.colours);
            for (size_t row = 0; row < ROWS; ++row) keyboard_->set_row(static_cast<uint8_t>(row), grid[row]);
            keyboard_->show();
        }
    } catch (const Error&) {
        keyboard_->close();  // opened again by the next devices()
        throw;
    }
}

}  // namespace wpc::razer
