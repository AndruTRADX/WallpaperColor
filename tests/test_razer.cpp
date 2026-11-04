// The razer driver without a keyboard: its settings, the key grid, the reports byte by byte, and
// that no keyboard is not an error.

#include <doctest/doctest.h>

#include "drivers/razer/application/features/list_devices/list_devices.hpp"
#include "drivers/razer/application/features/show_frame/show_frame.hpp"
#include "drivers/razer/domain/settings.hpp"
#include "drivers/razer/domain/zones.hpp"
#include "drivers/razer/infrastructure/services/keyboard.hpp"
#include "utils/error.hpp"

using namespace wpc;

TEST_CASE("keyboard settings") {
    using Values = std::map<std::string, DriverSettings::Value>;
    auto read = [](Values v) { return razer::read_settings(DriverSettings("keyboard", std::move(v))); };

    CHECK(read({}).direction == razer::Direction::Across);
    CHECK(read({{"direction", std::string("down")}}).direction == razer::Direction::Down);
    CHECK(read({{"direction", std::string("across")}}).direction == razer::Direction::Across);
    CHECK_THROWS_AS(read({{"direction", std::string("sideways")}}), Error);
    CHECK_THROWS_AS(read({{"direction", 1.0}}), Error);
}

TEST_CASE("the keyboard is one line of keys") {
    Device d = razer::keyboard_device();
    REQUIRE(d.zones.size() == 1);
    CHECK(d.zones[0].leds == razer::ROWS * razer::COLUMNS);
    CHECK(d.zones[0].component == Component::Keyboard);
    CHECK(d.zones[0].shape == Shape::Line);
}

TEST_CASE("the colours run across or down the keys") {
    std::vector<Rgb> colours(razer::ROWS * razer::COLUMNS);
    for (size_t i = 0; i < colours.size(); ++i) colours[i] = Rgb{static_cast<uint8_t>(i), 0, 0};

    // Across: column by column, so the first colours are the left column, top to bottom.
    razer::KeyGrid across = razer::place_keys({razer::Direction::Across}, colours);
    CHECK(across[0][0].r == 0);
    CHECK(across[1][0].r == 1);
    CHECK(across[0][1].r == razer::ROWS);
    CHECK(across[razer::ROWS - 1][razer::COLUMNS - 1].r == colours.size() - 1);

    // Down: row by row.
    razer::KeyGrid down = razer::place_keys({razer::Direction::Down}, colours);
    CHECK(down[0][1].r == 1);
    CHECK(down[1][0].r == razer::COLUMNS);

    // One colour: every key.
    std::vector<Rgb> red = {Rgb{255, 0, 0}};
    razer::KeyGrid all = razer::place_keys({}, red);
    CHECK(all[0][0] == red[0]);
    CHECK(all[razer::ROWS - 1][razer::COLUMNS - 1] == red[0]);
}

TEST_CASE("Razer reports, byte by byte") {
    // Row 1, every key red.
    std::vector<Rgb> red(razer::COLUMNS, Rgb{255, 0, 0});
    razer::HidKeyboard::Report row = razer::HidKeyboard::row_report(1, red);
    CHECK(row[0] == 0x00);   // report id
    CHECK(row[2] == 0x3F);   // transaction id
    CHECK(row[6] == 0x47);   // data size
    CHECK(row[7] == 0x0F);   // class
    CHECK(row[8] == 0x03);   // custom frame
    CHECK(row[11] == 1);     // row
    CHECK(row[12] == 0);     // first column
    CHECK(row[13] == 14);    // last column
    CHECK(row[14] == 255);   // the first key: red, green, blue
    CHECK(row[15] == 0);
    CHECK(row[16] == 0);
    CHECK(row[14 + 14 * 3] == 255);  // the last key
    CHECK(row[14 + 15 * 3] == 0);    // nothing after it
    CHECK(row[89] == 0xBB);  // 0x47 ^ 0x0F ^ 0x03 ^ 1 ^ 14 ^ 255 (15 times)

    razer::HidKeyboard::Report show = razer::HidKeyboard::show_report();
    razer::HidKeyboard::Report expected{};
    expected[2] = 0x3F;
    expected[6] = 0x0C;
    expected[7] = 0x0F;
    expected[8] = 0x02;
    expected[11] = 0x08;  // the custom effect
    expected[89] = 0x0C ^ 0x0F ^ 0x02 ^ 0x08;
    CHECK(show == expected);
}

namespace {

/// Remembers what it was told; fails when told to.
struct FakeKeyboard final : razer::RazerKeyboard {
    bool present = true;
    bool fail = false;
    bool opened = false;
    int opens = 0;
    int rows = 0;
    int shows = 0;
    bool open() override {
        ++opens;
        opened = present;
        return present;
    }
    void close() override { opened = false; }
    bool is_open() const override { return opened; }
    void set_row(uint8_t, std::span<const Rgb>) override {
        if (fail) throw Error("unplugged");
        ++rows;
    }
    void show() override { ++shows; }
};

}  // namespace

TEST_CASE("the keyboard is opened when needed, absent is no device, and a failure closes it") {
    FakeKeyboard keyboard;
    razer::KeyboardState state;

    keyboard.present = false;
    CHECK(razer::list_devices(keyboard, state).empty());

    keyboard.present = true;
    REQUIRE(razer::list_devices(keyboard, state).size() == 1);
    razer::list_devices(keyboard, state);
    CHECK(keyboard.opens == 2);  // kept open

    std::vector<Rgb> red = {Rgb{255, 0, 0}};
    ZoneFrame keys{0, red};
    razer::show_frame(keyboard, state, std::span(&keys, 1));
    CHECK(keyboard.rows == static_cast<int>(razer::ROWS));
    CHECK(keyboard.shows == 1);

    keyboard.fail = true;
    CHECK_THROWS_AS(razer::show_frame(keyboard, state, std::span(&keys, 1)), Error);
    CHECK_FALSE(keyboard.is_open());  // opened again by the next list_devices()
    CHECK_THROWS_WITH(razer::show_frame(keyboard, state, std::span(&keys, 1)), "Razer keyboard: not open");
}
