// The fusion2 driver without a board: its settings, and that no board is not an error.

#include <doctest/doctest.h>

#include "drivers/fusion2/application/features/list_devices/list_devices.hpp"
#include "drivers/fusion2/application/features/show_frame/show_frame.hpp"
#include "drivers/fusion2/domain/settings.hpp"
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
        opened = present;
        return present;
    }
    void close() override { opened = false; }
    bool is_open() const override { return opened; }
    void set_onboard(Rgb c) override {
        if (fail) throw Error("unplugged");
        onboard.push_back(c);
    }
    void set_header(int, const std::vector<Rgb>&) override {
        if (fail) throw Error("unplugged");
    }
};

}  // namespace

TEST_CASE("the board is opened when needed, absent is no device, and a failure closes it") {
    FakeBoard board;
    fusion2::BoardState state{{}, fusion2::board_layout({}), {}};

    board.present = false;
    CHECK(fusion2::list_devices(board, state).empty());

    board.present = true;
    REQUIRE(fusion2::list_devices(board, state).size() == 1);
    fusion2::list_devices(board, state);
    CHECK(board.opens == 2);  // kept open

    std::vector<Rgb> red = {Rgb{255, 0, 0}};
    ZoneFrame onboard{0, red};
    fusion2::show_frame(board, state, std::span(&onboard, 1));
    CHECK(board.onboard == red);

    board.fail = true;
    CHECK_THROWS_AS(fusion2::show_frame(board, state, std::span(&onboard, 1)), Error);
    CHECK_FALSE(board.is_open());  // opened again by the next list_devices()
    CHECK_THROWS_WITH(fusion2::show_frame(board, state, std::span(&onboard, 1)), "motherboard RGB: not open");
}
