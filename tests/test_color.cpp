#include <doctest/doctest.h>

#include "colours/colour.hpp"

using namespace wpc;

TEST_CASE("hex round trip") {
    std::optional<Rgb> c = parse_colour("#0a77ff");
    REQUIRE(c);
    CHECK(*c == Rgb{0x0a, 0x77, 0xff});
    CHECK(to_hex(*c) == "#0a77ff");
    CHECK(parse_colour("ff0000") == Rgb{255, 0, 0});
    CHECK_FALSE(parse_colour("#12345"));
    CHECK_FALSE(parse_colour("#gg0000"));
}

TEST_CASE("hsv round trip") {
    for (Rgb c : {Rgb{255, 0, 0}, Rgb{0, 255, 0}, Rgb{0, 0, 255}, Rgb{10, 119, 255},
                  Rgb{200, 30, 180}, Rgb{128, 128, 128}, Rgb{0, 0, 0}}) {
        CAPTURE(to_hex(c));
        CHECK(to_rgb(to_hsv(c)) == c);
    }
}

TEST_CASE("hsv values") {
    Hsv hsv = to_hsv(Rgb{255, 0, 0});
    CHECK(hsv.h == 0.0f);
    CHECK(hsv.s == 1.0f);
    CHECK(hsv.v == 1.0f);
    CHECK(to_hsv(Rgb{128, 128, 128}).s == 0.0f);
}

TEST_CASE("rem_euclid is never negative") {
    CHECK(rem_euclid(-30.0f, 360.0f) == 330.0f);
    CHECK(rem_euclid(390.0f, 360.0f) == 30.0f);
}

TEST_CASE("only hex digits") {
    CHECK_FALSE(parse_colour("#+f+f+f"));
    CHECK_FALSE(parse_colour("#ff00é"));
}
