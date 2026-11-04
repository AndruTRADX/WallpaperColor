#include "drivers/razer/domain/zones.hpp"

#include <algorithm>

namespace wpc::razer {

Device keyboard_device() {
    return Device{"huntsman-mini",
                  "Razer Huntsman Mini",
                  {Zone{"Keys", static_cast<uint32_t>(ROWS * COLUMNS), Component::Keyboard, Shape::Line}},
                  {}};
}

KeyGrid place_keys(const KeyboardSettings& settings, std::span<const Rgb> colours) {
    KeyGrid grid{};
    if (colours.empty()) return grid;
    for (size_t row = 0; row < ROWS; ++row) {
        for (size_t col = 0; col < COLUMNS; ++col) {
            size_t i = settings.direction == Direction::Across ? col * ROWS + row : row * COLUMNS + col;
            grid[row][col] = colours[std::min(i, colours.size() - 1)];
        }
    }
    return grid;
}

}  // namespace wpc::razer
