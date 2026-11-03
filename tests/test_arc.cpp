#include <doctest/doctest.h>

#include "shared/domain/colours/gradient.hpp"

using namespace wpc;

namespace {
const Rgb R{255, 0, 0};
const Rgb G{0, 255, 0};
const Rgb B{0, 0, 255};
}  // namespace

TEST_CASE("open gradient hits both ends") {
    std::vector<Rgb> g = gradient({R, G, B}, 10, false);
    CHECK(g.size() == 10);
    CHECK(g[0] == R);
    CHECK(g[9] == B);
}

TEST_CASE("closed gradient wraps without a jump") {
    std::vector<Rgb> g = gradient({R, G, B}, 9, true);
    CHECK(g[0] == R);
    CHECK(g[3] == G);
    CHECK(g[6] == B);
    // the last LED is between blue and red again (magenta side), not blue
    float h = g[8].to_hsv().h;
    CHECK(h >= 280.0f);
    CHECK(h < 340.0f);
}

TEST_CASE("mix takes the short way round and stays vivid") {
    Hsv h = mix(Rgb{255, 0, 40}, Rgb{255, 40, 0}, 0.5f).to_hsv();  // across hue 0
    CHECK((h.h < 10.0f || h.h > 350.0f));
    Hsv blue_orange = mix(B, Rgb{255, 128, 0}, 0.5f).to_hsv();
    CHECK(blue_orange.s > 0.9f);  // no grey midpoint
}

TEST_CASE("white end keeps the other hue") {
    Hsv m = mix(WHITE, B, 0.5f).to_hsv();
    CHECK(m.h >= 235.0f);
    CHECK(m.h < 245.0f);
}

TEST_CASE("gradient edge cases") {
    CHECK(gradient({R, G}, 0, true).empty());
    CHECK(gradient({R}, 3, false) == std::vector<Rgb>(3, R));
    CHECK(gradient({R, G, B}, 1, false) == std::vector<Rgb>{R});
}
