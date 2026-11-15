// Gigabyte RGB Fusion 2, the RGB controller on Gigabyte motherboards (an ITE IT5701/IT5702 or
// IT8297 chip, a USB HID device), as pure functions: the `[motherboard]` settings, the board as
// a device (its zones, and where their LEDs go), and the 64-byte HID feature reports that drive
// it. Sending them is board.cpp. The protocol comes from OpenRGB
// (Controllers/GigabyteRGBFusion2USBController).
//
// Two kinds of lights:
// - the board's own single-colour zones (I/O cover, the 12 V RGB header LED_C…): one colour
//   for all of them, set as a "static" effect;
// - the two ARGB headers (addressable fans or strips): every LED on its own ("direct" mode).
//   Their colour order (GRB for most fans) is applied here, so nothing is written to the chip.
//
// Nothing is ever saved to the board's flash memory: after a reboot it shows whatever the BIOS
// sets until the service paints it again.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "drivers/sdk/driver.hpp"

namespace wpc::fusion2 {

/// ARGB headers on the board.
inline constexpr int HEADERS = 2;

// ---------------------------------------------------------------------------------------------
// Settings

/// Where red, green and blue go in each LED's three bytes (by index: red, green, blue).
using ColourPositions = std::array<uint8_t, 3>;

/// The positions for a colour order like "GRB" (green is byte 0, red byte 1: {1, 0, 2});
/// nullopt unless `order` is R, G and B, once each.
std::optional<ColourPositions> colour_positions(std::string_view order);

/// The `[motherboard]` table.
struct BoardSettings {
    /// The colour order of what is plugged into the ARGB headers ("GRB" for most fans); always
    /// one colour_positions() accepts.
    std::string argb_order = "GRB";
    /// What is chained on ARGB header 1 and 2: the LEDs of each fan or strip, in order
    /// ([8, 8] = two fans of 8 LEDs). Empty = unknown: the whole header gets one colour.
    std::array<std::vector<uint32_t>, HEADERS> argb_headers;
};

/// Throws wpc::Error for a wrong value.
BoardSettings read_settings(const DriverSettings& table);

// ---------------------------------------------------------------------------------------------
// The board as a device: its onboard lights as one zone (component `motherboard`), then what is
// plugged into the two ARGB headers (component `argb`), one zone per fan when the settings say
// what is there, else one zone per header.

struct BoardLayout {
    /// Where a zone's LEDs go: header 0 or 1 from LED `first`, or the onboard lights (-1).
    struct Place {
        int header;
        size_t first;
    };
    Device device;
    std::vector<Place> places;  // one per zone of `device`
    /// LEDs each header holds (1 when its fans are unknown).
    std::array<size_t, HEADERS> header_leds{};
};

BoardLayout board_layout(const BoardSettings& settings);

/// What a frame changes: the onboard colour if its zone is in it, and each header whose zones
/// are in it (`headers` keeps every header's colours between frames).
struct BoardFrame {
    std::optional<Rgb> onboard;
    std::array<bool, HEADERS> header_changed{};
};

/// Throws wpc::Error for a zone the board doesn't have.
BoardFrame place_frame(const BoardLayout& layout, std::span<const ZoneFrame> zones,
                       std::array<std::vector<Rgb>, HEADERS>& headers);

// ---------------------------------------------------------------------------------------------
// Reports: report id 0xCC, a command, its arguments.

using Report = std::array<uint8_t, 64>;

inline constexpr uint8_t REPORT_ID = 0xCC;
inline constexpr uint8_t APPLY = 0x28;
inline constexpr uint8_t AUDIO_SYNC = 0x31;
inline constexpr uint8_t HEADER_EFFECTS_OFF = 0x32;  // bit mask of ARGB headers driven LED by LED
inline constexpr uint8_t HEADER_LENGTHS = 0x34;
inline constexpr uint8_t LAMP_ARRAY = 0x48;          // Windows "Dynamic Lighting"
inline constexpr uint8_t GET_INFO = 0x60;

/// Zones 5 and 6 are the ARGB headers, which are driven LED by LED instead.
inline constexpr uint8_t ONBOARD_ZONES = 0xFF & ~(1u << 5 | 1u << 6);
inline constexpr int ZONES = 8;
/// LEDs per report in direct mode (19 × 3 bytes after a 5-byte header).
inline constexpr size_t LEDS_PER_REPORT = 19;
/// LEDs a header drives unless told otherwise; also what an unknown count is painted as.
inline constexpr uint32_t DEFAULT_HEADER_LEDS = 32;

/// A report with just a command and up to two arguments.
Report command_report(uint8_t command, uint8_t a = 0, uint8_t b = 0);

/// The report that shows `colour` on onboard zone `zone` (0-7) as a static effect; APPLY
/// with ONBOARD_ZONES shows them.
Report zone_colour_report(int zone, Rgb colour);

/// The direct-mode report of header `header` (0 or 1) for `leds` (at most LEDS_PER_REPORT),
/// the first of them being LED number `first` of the header.
Report header_leds_report(int header, size_t first, std::span<const Rgb> leds, const ColourPositions& positions);

/// The chip's code for a header length: up to 32, 64, 256, 512 or 1024 LEDs.
uint8_t length_code(uint32_t leds);

}  // namespace wpc::fusion2
