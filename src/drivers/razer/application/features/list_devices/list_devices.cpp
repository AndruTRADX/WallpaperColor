#include "drivers/razer/application/features/list_devices/list_devices.hpp"

#include "drivers/razer/domain/zones.hpp"

namespace wpc::razer {

std::vector<Device> list_devices(RazerKeyboard& keyboard, const KeyboardState&) {
    if (keyboard.is_open() && !keyboard.still_connected()) keyboard.close();  // unplugged since
    if (!keyboard.is_open() && !keyboard.open()) return {};  // not plugged in is not an error
    return {keyboard_device()};
}

}  // namespace wpc::razer
