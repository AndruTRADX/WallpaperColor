// Feature: one frame on the keyboard: every row of keys, then shown at once.
// A failure closes the keyboard: the next list_devices() opens it again.

#pragma once

#include <span>

#include "drivers/razer/application/contracts/razer_keyboard.hpp"
#include "drivers/razer/application/models/keyboard_state.hpp"
#include "drivers/sdk/driver.hpp"

namespace wpc::razer {

/// Throws wpc::Error.
void show_frame(RazerKeyboard& keyboard, const KeyboardState& state, std::span<const ZoneFrame> zones);

}  // namespace wpc::razer
