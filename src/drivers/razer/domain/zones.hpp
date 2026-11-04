// The Razer Huntsman Mini as a device: its keys as one zone (component `keyboard`, a line), and
// where each of the zone's colours goes on the key grid.
//
// The keyboard lights a grid of 5 rows × 15 columns (a 60% keyboard: some cells have no key).
// The zone's LEDs are the cells in the order the colours should run: column by column (left to
// right) or row by row (top to bottom), as the settings say.

#pragma once

#include <array>
#include <span>

#include "drivers/razer/domain/settings.hpp"
#include "drivers/sdk/hardware.hpp"

namespace wpc::razer {

inline constexpr size_t ROWS = 5;
inline constexpr size_t COLUMNS = 15;

/// The colour of every cell of the grid, row by row.
using KeyGrid = std::array<std::array<Rgb, COLUMNS>, ROWS>;

Device keyboard_device();

/// Puts a zone's colours on the grid. One colour goes on every key; with fewer colours than
/// cells, the last cells keep the last colour.
KeyGrid place_keys(const KeyboardSettings& settings, std::span<const Rgb> colours);

}  // namespace wpc::razer
