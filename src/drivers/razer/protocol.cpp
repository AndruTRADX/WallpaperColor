#include "drivers/razer/protocol.hpp"

#include <algorithm>

#include "utils/error.hpp"
#include "utils/text.hpp"

namespace wpc::razer {

namespace {

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

Report report(uint8_t command, uint8_t data_size) {
    Report r{};
    r[TRANSACTION] = TRANSACTION_ID;
    r[DATA_SIZE] = data_size;
    r[CLASS] = MATRIX_CLASS;
    r[COMMAND] = command;
    return r;
}

void sign(Report& r) {
    uint8_t sum = 0;
    for (size_t i = 3; i < CHECKSUM; ++i) sum ^= r[i];  // bytes 2-87 of the report
    r[CHECKSUM] = sum;
}

}  // namespace

KeyboardSettings read_settings(const DriverSettings& table) {
    KeyboardSettings s;
    if (std::optional<std::string> direction = table.string("direction")) {
        if (*direction == "across") {
            s.direction = Direction::Across;
        } else if (*direction == "down") {
            s.direction = Direction::Down;
        } else {
            throw Error(cat("[", table.table(), "] direction: `", *direction, "` must be \"across\" or \"down\""));
        }
    }
    return s;
}

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

Report row_report(uint8_t row, std::span<const Rgb> colours) {
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

Report show_report() {
    Report r = report(SET_EFFECT, 0x0C);
    uint8_t* args = &r[ARGUMENTS];
    args[0] = 0;  // not saved
    args[1] = 0;  // every LED
    args[2] = EFFECT_CUSTOM;
    sign(r);
    return r;
}

}  // namespace wpc::razer
