// The fusion2 driver without a board: its settings, and that no board is not an error.

#include <doctest/doctest.h>

#include "drivers/fusion2/driver.hpp"
#include "drivers/fusion2/protocol.hpp"
#include "utils/error.hpp"

using namespace wpc;

TEST_CASE("motherboard settings") {
    using Values = std::map<std::string, DriverSettings::Value>;
    auto read = [](Values v) { return fusion2::read_settings(DriverSettings("motherboard", std::move(v))); };

    fusion2::BoardSettings defaults = read({});
    CHECK(defaults.argb_order == "GRB");
    CHECK(defaults.argb_headers[0].empty());

    fusion2::BoardSettings s = read({{"argb_order", std::string("RGB")}, {"argb_header1", std::vector<double>{8, 8}}});
    CHECK(s.argb_order == "RGB");
    CHECK(s.argb_headers[0] == std::vector<uint32_t>{8, 8});
    CHECK(s.argb_headers[1].empty());

    CHECK_THROWS_AS(read({{"argb_order", std::string("RRB")}}), Error);
    CHECK_THROWS_AS(read({{"argb_header1", 8.0}}), Error);              // not a list
    CHECK_THROWS_AS(read({{"argb_header1", std::vector<double>{8.5}}}), Error);

    // Every choice the Drivers page offers is one the driver reads; the first is the default.
    for (const char* order : fusion2::ARGB_ORDERS) CHECK(read({{"argb_order", std::string(order)}}).argb_order == order);
    CHECK(fusion2::ARGB_ORDERS[0] == defaults.argb_order);

    DriverSettings old("motherboard", {{"enabled", false}});  // from before [components]
    fusion2::read_settings(old);
    CHECK(old.unread().empty());
}

namespace {

/// Remembers what it was told; fails when told to.
struct FakeBoard final : fusion2::FusionBoard {
    bool present = true;
    bool fail = false;
    bool opened = false;
    int opens = 0;
    std::vector<Rgb> onboard;
    bool open(const fusion2::BoardSettings&) override {
        ++opens;
        connected = true;  // a new handle
        opened = present;
        return present;
    }
    void close() override { opened = false; }
    bool is_open() const override { return opened; }
    bool connected = true;  // false: unplugged since it was opened
    bool still_connected() const override { return connected; }
    void set_onboard(Rgb c) override {
        if (fail) throw Error("unplugged");
        onboard.push_back(c);
    }
    void set_header(int, std::span<const Rgb>) override {
        if (fail) throw Error("unplugged");
    }
};

/// The driver with a fake board, and the fake to steer it.
struct Board {
    FakeBoard* fake;
    fusion2::BoardDriver driver;
    Board() : Board(std::make_unique<FakeBoard>()) {}

private:
    explicit Board(std::unique_ptr<FakeBoard> b) : fake(b.get()), driver({}, std::move(b)) {}
};

}  // namespace

TEST_CASE("the board is opened when needed, absent is no device, and a failure closes it") {
    Board b;
    FakeBoard& board = *b.fake;

    board.present = false;
    CHECK(b.driver.devices().empty());

    board.present = true;
    REQUIRE(b.driver.devices().size() == 1);
    b.driver.devices();
    CHECK(board.opens == 2);  // kept open

    std::vector<Rgb> red = {Rgb{255, 0, 0}};
    ZoneFrame onboard{0, red};
    b.driver.show(0, std::span(&onboard, 1));
    CHECK(board.onboard == red);

    board.fail = true;
    CHECK_THROWS_AS(b.driver.show(0, std::span(&onboard, 1)), Error);
    CHECK_FALSE(board.is_open());  // opened again by the next devices()
    CHECK_THROWS_WITH(b.driver.show(0, std::span(&onboard, 1)), "motherboard RGB: not open");
}

TEST_CASE("a board controller that came back is opened again before painting, not failed on") {
    Board b;
    REQUIRE(b.driver.devices().size() == 1);

    b.fake->connected = false;
    REQUIRE(b.driver.devices().size() == 1);
    CHECK(b.fake->opens == 2);
    b.driver.devices();
    CHECK(b.fake->opens == 2);  // the new one is kept
}

TEST_CASE("colour orders: R, G and B once each, anything else refused") {
    CHECK(fusion2::colour_positions("GRB") == fusion2::ColourPositions{1, 0, 2});
    CHECK(fusion2::colour_positions("RGB") == fusion2::ColourPositions{0, 1, 2});
    CHECK(fusion2::colour_positions("BGR") == fusion2::ColourPositions{2, 1, 0});
    for (const char* bad : {"", "RG", "RGBB", "RRB", "XYZ", "rgb"}) {
        CAPTURE(bad);
        CHECK_FALSE(fusion2::colour_positions(bad));
    }
}

TEST_CASE("Fusion 2 reports, byte by byte") {
    // Header 2, LEDs 19 and 20 of a GRB fan: offset 57 bytes, 6 bytes of colour.
    std::vector<Rgb> leds = {Rgb{10, 20, 30}, Rgb{40, 50, 60}};
    fusion2::Report r = fusion2::header_leds_report(1, 19, leds, *fusion2::colour_positions("GRB"));
    CHECK(r[0] == 0xCC);
    CHECK(r[1] == 0x59);  // header 2
    CHECK(r[2] == 57);
    CHECK(r[3] == 0);
    CHECK(r[4] == 6);
    CHECK(std::vector<uint8_t>(r.begin() + 5, r.begin() + 11) == std::vector<uint8_t>{20, 10, 30, 50, 40, 60});
    CHECK(r[11] == 0);

    fusion2::Report zone = fusion2::zone_colour_report(3, Rgb{1, 2, 3});
    CHECK(zone[1] == 0x23);
    CHECK(zone[2] == 0x08);
    CHECK(zone[11] == 1);  // static
    CHECK(std::vector<uint8_t>(zone.begin() + 14, zone.begin() + 17) == std::vector<uint8_t>{3, 2, 1});  // blue, green, red

    CHECK(fusion2::length_code(32) == 0);
    CHECK(fusion2::length_code(33) == 1);
    CHECK(fusion2::length_code(1024) == 4);
}
