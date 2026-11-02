#include "fusion2/board.hpp"

#include <cerrno>
#include <algorithm>
#include <format>
#include <fstream>

#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include "util/error.hpp"

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

/// USB vendor and product ids of the controllers this speaks to.
constexpr const char* KNOWN_IDS[] = {"0000048D:00005702", "0000048D:00008297"};

/// The /dev/hidraw* of the controller, found through /sys; nullopt if there is none.
std::optional<fs::path> find_device() {
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator("/sys/class/hidraw", ec)) {
        std::ifstream uevent(entry.path() / "device" / "uevent");
        std::string line;
        while (std::getline(uevent, line)) {
            if (!line.starts_with("HID_ID=")) continue;
            for (const char* id : KNOWN_IDS) {
                if (line.ends_with(id)) return fs::path("/dev") / entry.path().filename();
            }
        }
    }
    return std::nullopt;
}

}  // namespace

Board::Board(Fd fd, fs::path path, const MotherboardConfig& config)
    : fd_(std::move(fd)), path_(std::move(path)), header_fans_(config.argb_headers) {
    for (int i = 0; i < 3; ++i) {
        // order_ = where red, green and blue go: for "GRB", green is byte 0, red byte 1.
        order_[std::string_view("RGB").find(config.argb_order[i])] = i;
    }
}

std::optional<Board> Board::open(const MotherboardConfig& config) {
    std::optional<fs::path> path = find_device();
    if (!path) return std::nullopt;

    Fd fd(::open(path->c_str(), O_RDWR | O_CLOEXEC));
    if (!fd.valid()) {
        int error = errno;
        throw Error(std::format("cannot open the motherboard RGB controller {}: {}{}", path->string(),
                                os_error(error),
                                error == EACCES ? " (install udev/70-wallpapercolor.rules)" : ""));
    }
    Board board(std::move(fd), *path, config);

    // Ask what it is. The answer: report id, product, device number, strip detection,
    // firmware version (4 bytes), ARGB LED counts (3 bytes), feature flags, product name (28).
    board.command(GET_INFO);
    Report info = board.receive();
    board.product_ = std::string(reinterpret_cast<const char*>(&info[12]), 28);
    board.product_ = board.product_.substr(0, board.product_.find('\0'));
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

uint32_t Board::header_leds(int header) const {
    uint32_t sum = 0;
    for (uint32_t leds : header_fans_[header]) sum += leds;
    return sum;
}

std::vector<Board::Zone> Board::header_zones(int header) const {
    if (header_fans_[header].empty()) return {Zone{HEADER_ZONE, 1}};
    std::vector<Zone> zones;
    for (uint32_t leds : header_fans_[header]) zones.push_back(Zone{FAN_ZONE, leds});
    return zones;
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
    Report copy = report;
    if (::ioctl(fd_.get(), HIDIOCSFEATURE(copy.size()), copy.data()) < 0) {
        throw Error("motherboard RGB: " + os_error(errno));
    }
}

Board::Report Board::receive() {
    Report r{};
    r[0] = REPORT_ID;
    if (::ioctl(fd_.get(), HIDIOCGFEATURE(r.size()), r.data()) < 0) {
        throw Error("motherboard RGB: " + os_error(errno));
    }
    return r;
}

}  // namespace wpc::fusion2
