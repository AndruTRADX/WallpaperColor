#include "drivers/razer/infrastructure/services/keyboard.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>

#include "utils/error.hpp"
#include "utils/hidraw.hpp"

namespace wpc::razer {

namespace {

/// The Huntsman Mini, through the USB interface that drives its lights.
constexpr hidraw::UsbId KNOWN_IDS[] = {{0x1532, 0x0257, 2}};

/// The transaction id the Huntsman Mini answers to.
constexpr uint8_t TRANSACTION_ID = 0x3F;

constexpr uint8_t MATRIX_CLASS = 0x0F;
constexpr uint8_t SET_EFFECT = 0x02;
constexpr uint8_t SET_CUSTOM_FRAME = 0x03;
constexpr uint8_t EFFECT_CUSTOM = 0x08;
/// The data size every custom frame report says it has (what openrazer sends).
constexpr uint8_t CUSTOM_FRAME_SIZE = 0x47;

/// Where things are in a Report (byte 0 is the report id).
constexpr size_t TRANSACTION = 2;
constexpr size_t DATA_SIZE = 6;
constexpr size_t CLASS = 7;
constexpr size_t COMMAND = 8;
constexpr size_t ARGUMENTS = 9;
constexpr size_t CHECKSUM = 89;

/// Colours a row report holds: 80 bytes of arguments, 5 before the colours.
constexpr size_t MAX_ROW_COLOURS = (80 - 5) / 3;

HidKeyboard::Report report(uint8_t command, uint8_t data_size) {
    HidKeyboard::Report r{};
    r[TRANSACTION] = TRANSACTION_ID;
    r[DATA_SIZE] = data_size;
    r[CLASS] = MATRIX_CLASS;
    r[COMMAND] = command;
    return r;
}

void sign(HidKeyboard::Report& r) {
    uint8_t sum = 0;
    for (size_t i = 3; i < CHECKSUM; ++i) sum ^= r[i];  // bytes 2-87 of the report
    r[CHECKSUM] = sum;
}

}  // namespace

HidKeyboard::Report HidKeyboard::row_report(uint8_t row, std::span<const Rgb> colours) {
    size_t count = std::min(colours.size(), MAX_ROW_COLOURS);
    Report r = report(SET_CUSTOM_FRAME, CUSTOM_FRAME_SIZE);
    uint8_t* args = &r[ARGUMENTS];
    args[2] = row;
    args[3] = 0;  // first column
    args[4] = static_cast<uint8_t>(count == 0 ? 0 : count - 1);  // last column
    for (size_t i = 0; i < count; ++i) {
        args[5 + i * 3] = colours[i].r;
        args[6 + i * 3] = colours[i].g;
        args[7 + i * 3] = colours[i].b;
    }
    sign(r);
    return r;
}

HidKeyboard::Report HidKeyboard::show_report() {
    Report r = report(SET_EFFECT, 0x0C);
    uint8_t* args = &r[ARGUMENTS];
    args[0] = 0;  // not saved
    args[1] = 0;  // every LED
    args[2] = EFFECT_CUSTOM;
    sign(r);
    return r;
}

bool HidKeyboard::open() {
    std::optional<std::filesystem::path> path = hidraw::find(KNOWN_IDS);
    if (!path) return false;
    try {
        fd_ = hidraw::open(*path, "install udev/70-wallpapercolor.rules");
    } catch (const Error& e) {
        throw Error(cat("Razer keyboard: ", e.what()));
    }
    return true;
}

bool HidKeyboard::still_connected() const {
    return hidraw::connected(fd_);
}

void HidKeyboard::send(const Report& report) {
    try {
        hidraw::set_feature(fd_, report);
    } catch (const Error& e) {
        throw Error(cat("Razer keyboard: ", e.what()));
    }
}

}  // namespace wpc::razer
