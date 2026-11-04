// Model: what the razer driver keeps between calls.

#pragma once

#include "drivers/razer/domain/settings.hpp"

namespace wpc::razer {

struct KeyboardState {
    KeyboardSettings settings;
};

}  // namespace wpc::razer
