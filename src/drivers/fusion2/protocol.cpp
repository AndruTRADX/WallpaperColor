#include "drivers/fusion2/protocol.hpp"

#include <algorithm>
#include <cmath>

#include "utils/error.hpp"
#include "utils/text.hpp"

namespace wpc::fusion2 {

namespace {

/// Names as the user sees them (and matches them in [[calibration]]). Every fan is called the
/// same, so split mode gives all of them the same colour.
constexpr const char* DEVICE_NAME = "Motherboard";
constexpr const char* ONBOARD_ZONE = "Onboard";
constexpr const char* HEADER_ZONE = "ARGB header";
constexpr const char* FAN_ZONE = "ARGB fan";

constexpr uint8_t SET_ZONE_EFFECT = 0x20;  // + zone number (0-7)
constexpr uint8_t HEADER_LEDS[HEADERS] = {0x58, 0x59};  // per-LED colours of ARGB header 1, 2
constexpr uint8_t EFFECT_STATIC = 1;

}  // namespace

// ---------------------------------------------------------------------------------------------
// Settings

std::optional<ColourPositions> colour_positions(std::string_view order) {
    if (order.size() != 3) return std::nullopt;
    ColourPositions positions{};
    bool seen[3] = {false, false, false};
    for (uint8_t i = 0; i < 3; ++i) {
        size_t channel = std::string_view("RGB").find(order[i]);
        if (channel == std::string_view::npos || seen[channel]) return std::nullopt;
        seen[channel] = true;
        positions[channel] = i;
    }
    return positions;
}

BoardSettings read_settings(const DriverSettings& table) {
    BoardSettings s;
    table.boolean("enabled");  // the old on/off switch, now [components]: read by the core
    if (std::optional<std::string> order = table.string("argb_order")) {
        if (!colour_positions(*order)) {
            throw Error(cat("[", table.table(), "] argb_order: `", *order, "` must have R, G and B in some order, like \"GRB\""));
        }
        s.argb_order = *order;
    }
    for (int h = 0; h < HEADERS; ++h) {
        std::string key = cat("argb_header", h + 1);
        std::optional<std::vector<double>> fans = table.numbers(key);
        if (!fans) continue;
        for (double leds : *fans) {
            if (!(leds >= 0 && leds <= 1024) || leds != std::floor(leds)) {
                throw Error(cat("[", table.table(), "] ", key, ": ", leds, " is not a number of LEDs (0-1024)"));
            }
            s.argb_headers[h].push_back(static_cast<uint32_t>(leds));
        }
    }
    return s;
}

// ---------------------------------------------------------------------------------------------
// The board as a device

BoardLayout board_layout(const BoardSettings& settings) {
    BoardLayout layout;
    layout.device = Device{"motherboard", DEVICE_NAME, {}, {}};
    layout.device.zones.push_back(Zone{ONBOARD_ZONE, 1, Component::Motherboard, Shape::Spot});
    layout.places.push_back({-1, 0});
    for (int h = 0; h < HEADERS; ++h) {
        const std::vector<uint32_t>& fans = settings.argb_headers[h];
        if (fans.empty()) {
            // Unknown: one colour, which the board puts on every LED the header drives.
            layout.device.zones.push_back(Zone{HEADER_ZONE, 1, Component::Argb, Shape::Ring});
            layout.places.push_back({h, 0});
            layout.header_leds[h] = 1;
            continue;
        }
        size_t first = 0;
        for (uint32_t leds : fans) {
            layout.device.zones.push_back(Zone{FAN_ZONE, leds, Component::Argb, Shape::Ring});
            layout.places.push_back({h, first});
            first += leds;
        }
        layout.header_leds[h] = first;
    }
    return layout;
}

BoardFrame place_frame(const BoardLayout& layout, std::span<const ZoneFrame> zones,
                       std::array<std::vector<Rgb>, HEADERS>& headers) {
    BoardFrame frame;
    for (int h = 0; h < HEADERS; ++h) headers[h].resize(layout.header_leds[h]);
    for (const ZoneFrame& z : zones) {
        if (z.zone >= layout.places.size()) throw Error(cat("motherboard RGB: no zone ", z.zone));
        const BoardLayout::Place& p = layout.places[z.zone];
        if (p.header < 0) {
            if (!z.colours.empty()) frame.onboard = z.colours[0];
            continue;
        }
        // A header is sent whole: zones not in this frame keep what they showed.
        std::vector<Rgb>& header = headers[p.header];
        size_t n = std::min(z.colours.size(), header.size() - std::min(p.first, header.size()));
        std::copy_n(z.colours.begin(), n, header.begin() + static_cast<std::ptrdiff_t>(p.first));
        frame.header_changed[p.header] = true;
    }
    return frame;
}

// ---------------------------------------------------------------------------------------------
// Reports

Report command_report(uint8_t command, uint8_t a, uint8_t b) {
    Report r{};
    r[0] = REPORT_ID;
    r[1] = command;
    r[2] = a;
    r[3] = b;
    return r;
}

Report zone_colour_report(int zone, Rgb colour) {
    Report r{};
    r[0] = REPORT_ID;
    r[1] = static_cast<uint8_t>(SET_ZONE_EFFECT + zone);
    r[2] = static_cast<uint8_t>(1u << zone);  // which zones (a bit mask, 4 bytes)
    r[11] = EFFECT_STATIC;
    r[12] = 255;  // brightness: maximum
    r[13] = 0;    // brightness: minimum
    r[14] = colour.b;  // the colour, in blue, green, red order
    r[15] = colour.g;
    r[16] = colour.r;
    return r;
}

Report header_leds_report(int header, size_t first, std::span<const Rgb> leds, const ColourPositions& positions) {
    // Command, byte offset (2 bytes), byte count, then the colours.
    size_t count = std::min(LEDS_PER_REPORT, leds.size());
    size_t offset = first * 3;
    Report r{};
    r[0] = REPORT_ID;
    r[1] = HEADER_LEDS[header];
    r[2] = static_cast<uint8_t>(offset);
    r[3] = static_cast<uint8_t>(offset >> 8);
    r[4] = static_cast<uint8_t>(count * 3);
    for (size_t i = 0; i < count; ++i) {
        uint8_t* led = &r[5 + i * 3];
        led[positions[0]] = leds[i].r;
        led[positions[1]] = leds[i].g;
        led[positions[2]] = leds[i].b;
    }
    return r;
}

uint8_t length_code(uint32_t leds) {
    if (leds <= 32) return 0;
    if (leds <= 64) return 1;
    if (leds <= 256) return 2;
    if (leds <= 512) return 3;
    return 4;
}

}  // namespace wpc::fusion2
