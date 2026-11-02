#include "fusion2/board.hpp"

#include <cerrno>
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
constexpr uint8_t LAMP_ARRAY = 0x48;       // Windows "Dynamic Lighting"
constexpr uint8_t GET_INFO = 0x60;

constexpr uint8_t EFFECT_STATIC = 1;
constexpr int ZONES = 8;

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

std::optional<Board> Board::open() {
    std::optional<fs::path> path = find_device();
    if (!path) return std::nullopt;

    Fd fd(::open(path->c_str(), O_RDWR | O_CLOEXEC));
    if (!fd.valid()) {
        int error = errno;
        throw Error(std::format("cannot open the motherboard RGB controller {}: {}{}", path->string(),
                                os_error(error),
                                error == EACCES ? " (install udev/70-wallpapercolor.rules)" : ""));
    }
    Board board(std::move(fd), *path);

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
    return board;
}

void Board::set_colour(Rgb colour) {
    for (int zone = 0; zone < ZONES; ++zone) {
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
    command(APPLY, 0xFF);  // apply to all eight zones
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
