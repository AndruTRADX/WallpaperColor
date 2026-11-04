#include "drivers/razer/application/features/show_frame/show_frame.hpp"

#include "drivers/razer/domain/zones.hpp"
#include "utils/error.hpp"

namespace wpc::razer {

void show_frame(RazerKeyboard& keyboard, const KeyboardState& state, std::span<const ZoneFrame> zones) {
    if (!keyboard.is_open()) throw Error("Razer keyboard: not open");
    try {
        for (const ZoneFrame& z : zones) {  // the keyboard has one zone
            KeyGrid grid = place_keys(state.settings, z.colours);
            for (size_t row = 0; row < ROWS; ++row) keyboard.set_row(static_cast<uint8_t>(row), grid[row]);
            keyboard.show();
        }
    } catch (const Error&) {
        keyboard.close();  // opened again by the next list_devices()
        throw;
    }
}

}  // namespace wpc::razer
