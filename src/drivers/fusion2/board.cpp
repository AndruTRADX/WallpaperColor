#include "drivers/fusion2/board.hpp"

#include <algorithm>
#include <filesystem>

#include "utils/error.hpp"
#include "utils/hidraw.hpp"
#include "utils/text.hpp"

namespace wpc::fusion2 {

namespace {

/// USB ids of the controllers this speaks to (ITE IT5702 and IT8297).
constexpr hidraw::UsbId KNOWN_IDS[] = {{0x048D, 0x5702}, {0x048D, 0x8297}};

}  // namespace

Board::Board(Fd fd, const BoardSettings& settings, ColourPositions positions)
    : fd_(std::move(fd)), positions_(positions), header_fans_(settings.argb_headers) {}

std::optional<Board> Board::find(const BoardSettings& settings) {
    std::optional<ColourPositions> positions = colour_positions(settings.argb_order);
    if (!positions) throw Error(cat("motherboard RGB: argb_order `", settings.argb_order, "` is not an order of R, G and B"));
    std::optional<std::filesystem::path> path = hidraw::find(KNOWN_IDS);
    if (!path) return std::nullopt;
    Board board(hidraw::open(*path, "install udev/70-wallpapercolor.rules"), settings, *positions);

    // Ask what it is. The answer: report id, product, device number, strip detection,
    // firmware version (4 bytes), ARGB LED counts (3 bytes), feature flags, product name (28).
    board.send(command_report(GET_INFO));
    Report info = board.receive();
    uint8_t features = info[11];

    // Turn off what would fight us: Windows Dynamic Lighting, and sync with the audio.
    if (features & 0x02) board.send(command_report(LAMP_ARRAY, 0));
    board.send(command_report(AUDIO_SYNC, 0));

    // Drive both ARGB headers LED by LED; tell the chip their length if over the default.
    board.send(command_report(HEADER_EFFECTS_OFF, 0x03));
    if (board.header_leds(0) > DEFAULT_HEADER_LEDS || board.header_leds(1) > DEFAULT_HEADER_LEDS) {
        board.send(command_report(HEADER_LENGTHS, static_cast<uint8_t>(length_code(board.header_leds(1)) << 4 |
                                                                       length_code(board.header_leds(0)))));
    }
    return board;
}

bool Board::open(const BoardSettings& settings) {
    std::optional<Board> found = find(settings);
    if (!found) return false;
    *this = std::move(*found);
    return true;
}

bool Board::still_connected() const {
    return hidraw::connected(fd_);
}

uint32_t Board::header_leds(int header) const {
    uint32_t sum = 0;
    for (uint32_t leds : header_fans_[header]) sum += leds;
    return sum;
}

void Board::set_header(int header, std::span<const Rgb> colours) {
    // An unknown count: the one colour on every LED the header can drive.
    std::array<Rgb, DEFAULT_HEADER_LEDS> one_colour;
    std::span<const Rgb> leds = colours;
    if (header_fans_[header].empty() && !colours.empty()) {
        one_colour.fill(colours[0]);
        leds = one_colour;
    }
    for (size_t first = 0; first < leds.size(); first += LEDS_PER_REPORT) {
        size_t count = std::min(LEDS_PER_REPORT, leds.size() - first);
        send(header_leds_report(header, first, leds.subspan(first, count), positions_));
    }
}

void Board::set_onboard(Rgb colour) {
    for (int zone = 0; zone < ZONES; ++zone) {
        if (ONBOARD_ZONES & (1u << zone)) send(zone_colour_report(zone, colour));
    }
    send(command_report(APPLY, ONBOARD_ZONES));
}

void Board::send(const Report& report) {
    try {
        hidraw::set_feature(fd_, report);
    } catch (const Error& e) {
        throw Error(cat("motherboard RGB: ", e.what()));
    }
}

Report Board::receive() {
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
