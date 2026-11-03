#include <doctest/doctest.h>

#include "shared/domain/colours/color.hpp"

using namespace wpc;

TEST_CASE("hex round trip") {
    std::optional<Rgb> c = Rgb::parse("#0a77ff");
    REQUIRE(c);
    CHECK(*c == Rgb{0x0a, 0x77, 0xff});
    CHECK(c->hex() == "#0a77ff");
    CHECK(Rgb::parse("ff0000") == Rgb{255, 0, 0});
    CHECK_FALSE(Rgb::parse("#12345"));
    CHECK_FALSE(Rgb::parse("#gg0000"));
}

TEST_CASE("hsv round trip") {
    for (Rgb c : {Rgb{255, 0, 0}, Rgb{0, 255, 0}, Rgb{0, 0, 255}, Rgb{10, 119, 255},
                  Rgb{200, 30, 180}, Rgb{128, 128, 128}, Rgb{0, 0, 0}}) {
        CAPTURE(c.hex());
        CHECK(Rgb::from_hsv(c.to_hsv()) == c);
    }
}

TEST_CASE("hsv values") {
    Hsv hsv = Rgb{255, 0, 0}.to_hsv();
    CHECK(hsv.h == 0.0f);
    CHECK(hsv.s == 1.0f);
    CHECK(hsv.v == 1.0f);
    CHECK(Rgb{128, 128, 128}.to_hsv().s == 0.0f);
}

TEST_CASE("rem_euclid is never negative") {
    CHECK(rem_euclid(-30.0f, 360.0f) == 330.0f);
    CHECK(rem_euclid(390.0f, 360.0f) == 30.0f);
}

TEST_CASE("only hex digits") {
    CHECK_FALSE(Rgb::parse("#+f+f+f"));
    CHECK_FALSE(Rgb::parse("#ff00é"));
}
