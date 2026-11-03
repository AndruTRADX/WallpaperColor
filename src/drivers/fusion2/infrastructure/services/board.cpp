#include "drivers/fusion2/infrastructure/services/board.hpp"

#include <algorithm>

#include "utils/error.hpp"
#include "utils/hidraw.hpp"

namespace wpc::fusion2 {

namespace fs = std::filesystem;

namespace {

constexpr uint8_t REPORT_ID = 0xCC;

// Commands (the second byte of a report).
constexpr uint8_t SET_ZONE_EFFECT = 0x20;  // + zone number (0-7)
constexpr uint8_t APPLY = 0x28;
constexpr uint8_t AUDIO_SYNC = 0x31;
constexpr uint8_t HEADER_EFFECTS_OFF = 0x32;  // bit mask of ARGB headers driven LED by LED
constexpr uint8_t HEADER_LENGTHS = 0x34;
constexpr uint8_t LAMP_ARRAY = 0x48;          // Windows "Dynamic Lighting"
constexpr uint8_t HEADER_LEDS[HEADERS] = {0x58, 0x59};  // per-LED colours of ARGB header 1, 2
constexpr uint8_t GET_INFO = 0x60;

constexpr uint8_t EFFECT_STATIC = 1;
constexpr int ZONES = 8;
/// Zones 5 and 6 are the ARGB headers, which are driven LED by LED instead.
constexpr uint8_t ONBOARD_ZONES = 0xFF & ~(1u << 5 | 1u << 6);
/// LEDs per report in direct mode (19 × 3 bytes after a 5-byte header).
constexpr size_t LEDS_PER_REPORT = 19;
/// LEDs a header drives unless told otherwise; also what an unknown count is painted as.
constexpr uint32_t DEFAULT_HEADER_LEDS = 32;

/// The chip's code for a header length: up to 32, 64, 256, 512 or 1024 LEDs.
uint8_t length_code(uint32_t leds) {
    if (leds <= 32) return 0;
    if (leds <= 64) return 1;
    if (leds <= 256) return 2;
    if (leds <= 512) return 3;
    return 4;
}

/// USB ids of the controllers this speaks to (ITE IT5702 and IT8297).
constexpr hidraw::UsbId KNOWN_IDS[] = {{0x048D, 0x5702}, {0x048D, 0x8297}};

}  // namespace

Board::Board(Fd fd, const BoardSettings& settings) : fd_(std::move(fd)), header_fans_(settings.argb_headers) {
    for (int i = 0; i < 3; ++i) {
        // order_ = where red, green and blue go: for "GRB", green is byte 0, red byte 1.
        order_[std::string_view("RGB").find(settings.argb_order[i])] = i;
    }
}

std::optional<Board> Board::find(const BoardSettings& settings) {
    std::optional<fs::path> path = hidraw::find(KNOWN_IDS);
    if (!path) return std::nullopt;
    Board board(hidraw::open(*path, "install udev/70-wallpapercolor.rules"), settings);

    // Ask what it is. The answer: report id, product, device number, strip detection,
    // firmware version (4 bytes), ARGB LED counts (3 bytes), feature flags, product name (28).
    board.command(GET_INFO);
    Report info = board.receive();
    uint8_t features = info[11];

    // Turn off what would fight us: Windows Dynamic Lighting, and sync with the audio.
    if (features & 0x02) board.command(LAMP_ARRAY, 0);
    board.command(AUDIO_SYNC, 0);

    // Drive both ARGB headers LED by LED; tell the chip their length if over the default.
    board.command(HEADER_EFFECTS_OFF, 0x03);
    if (board.header_leds(0) > DEFAULT_HEADER_LEDS || board.header_leds(1) > DEFAULT_HEADER_LEDS) {
        board.command(HEADER_LENGTHS,
                      static_cast<uint8_t>(length_code(board.header_leds(1)) << 4 | length_code(board.header_leds(0))));
    }
    return board;
}

bool Board::open(const BoardSettings& settings) {
    std::optional<Board> found = find(settings);
    if (!found) return false;
    *this = std::move(*found);
    return true;
}

uint32_t Board::header_leds(int header) const {
    uint32_t sum = 0;
    for (uint32_t leds : header_fans_[header]) sum += leds;
    return sum;
}

void Board::set_header(int header, const std::vector<Rgb>& colours) {
    // An unknown count: the one colour on every LED the header can drive.
    std::vector<Rgb> leds = colours;
    if (header_fans_[header].empty() && !colours.empty()) leds.assign(DEFAULT_HEADER_LEDS, colours[0]);

    // Reports of up to 19 LEDs: command, byte offset (2 bytes), byte count, then the colours.
    for (size_t first = 0; first < leds.size(); first += LEDS_PER_REPORT) {
        size_t count = std::min(LEDS_PER_REPORT, leds.size() - first);
        size_t offset = first * 3;
        Report r{};
        r[0] = REPORT_ID;
        r[1] = HEADER_LEDS[header];
        r[2] = static_cast<uint8_t>(offset);
        r[3] = static_cast<uint8_t>(offset >> 8);
        r[4] = static_cast<uint8_t>(count * 3);
        for (size_t i = 0; i < count; ++i) {
            const Rgb& c = leds[first + i];
            uint8_t* led = &r[5 + i * 3];
            led[order_[0]] = c.r;
            led[order_[1]] = c.g;
            led[order_[2]] = c.b;
        }
        send(r);
    }
}

void Board::set_onboard(Rgb colour) {
    for (int zone = 0; zone < ZONES; ++zone) {
        if (!(ONBOARD_ZONES & (1u << zone))) continue;
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
        send(r);
    }
    command(APPLY, ONBOARD_ZONES);
}

void Board::command(uint8_t command, uint8_t a, uint8_t b) {
    Report r{};
    r[0] = REPORT_ID;
    r[1] = command;
    r[2] = a;
    r[3] = b;
    send(r);
}

void Board::send(const Report& report) {
    try {
        hidraw::set_feature(fd_, report);
    } catch (const Error& e) {
        throw Error(cat("motherboard RGB: ", e.what()));
    }
}

Board::Report Board::receive() {
    Report r{};
    r[0] = REPORT_ID;
    try {
        hidraw::get_feature(fd_, r);
    } catch (const Error& e) {
        throw Error(cat("motherboard RGB: ", e.what()));
    }
    return r;
}

}  // namespace wpc::fusion2
