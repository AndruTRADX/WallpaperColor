// A Razer Chroma keyboard, today the Razer Huntsman Mini (USB 1532:0257), as pure functions:
// the `[keyboard]` settings, the keyboard as a device, where each colour goes on the key grid,
// and the reports that light it. Sending them is keyboard.cpp. The protocol comes from openrazer
// (driver/razerchromacommon.c) and OpenRGB (Controllers/RazerController).
//
// Every message is a 90-byte HID feature report (report id 0) sent to the keyboard's USB
// interface 2, the one for its lights:
//
//   status, transaction id, remaining packets (2), protocol, data size, command class,
//   command id, 80 bytes of arguments, checksum (XOR of bytes 2-87), reserved
//
// A frame is one "custom frame" report per row of keys (class 0x0F, command 0x03), then the
// "custom" effect (class 0x0F, command 0x02), which shows them. Nothing is ever saved to the
// keyboard's memory: unplugged and plugged in again, it shows its own effect until the service
// paints it again.

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "drivers/sdk/driver.hpp"

namespace wpc::razer {

// ---------------------------------------------------------------------------------------------
// Settings

/// Which way the colours run over the keys.
enum class Direction : uint8_t {
    Across,  // left to right
    Down,    // top row to bottom row
};

/// The `[keyboard]` table.
struct KeyboardSettings {
    Direction direction = Direction::Across;
};

/// Throws wpc::Error for a wrong value.
KeyboardSettings read_settings(const DriverSettings& table);

// ---------------------------------------------------------------------------------------------
// The keyboard as a device: its keys as one zone (component `keyboard`, a line). It lights a
// grid of 5 rows × 15 columns (a 60% keyboard: some cells have no key); the zone's LEDs are the
// cells in the order the colours should run: column by column (left to right) or row by row
// (top to bottom), as the settings say.

inline constexpr size_t ROWS = 5;
inline constexpr size_t COLUMNS = 15;

/// The colour of every cell of the grid, row by row.
using KeyGrid = std::array<std::array<Rgb, COLUMNS>, ROWS>;

Device keyboard_device();

/// Puts a zone's colours on the grid. One colour goes on every key; with fewer colours than
/// cells, the last cells keep the last colour.
KeyGrid place_keys(const KeyboardSettings& settings, std::span<const Rgb> colours);

// ---------------------------------------------------------------------------------------------
// Reports

/// What goes to the device: report id 0, then the 90-byte report.
using Report = std::array<uint8_t, 91>;

/// The report that loads row `row`, from column 0 (at most 25 colours: the rest are left out).
Report row_report(uint8_t row, std::span<const Rgb> colours);

/// The report that shows the rows loaded (the "custom" effect).
Report show_report();

}  // namespace wpc::razer
