// Feature: the keyboard's device, when it is plugged in. Opens the keyboard if it isn't open.

#pragma once

#include <vector>

#include "drivers/razer/application/contracts/razer_keyboard.hpp"
#include "drivers/razer/application/models/keyboard_state.hpp"

namespace wpc::razer {

/// One device when the keyboard is there, none otherwise. Throws wpc::Error.
std::vector<Device> list_devices(RazerKeyboard& keyboard, const KeyboardState& state);

}  // namespace wpc::razer
