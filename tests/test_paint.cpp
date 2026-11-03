#include <doctest/doctest.h>

#include "shared/domain/colours/paint.hpp"

using namespace wpc;

namespace {
const Rgb R{255, 0, 0};
const Rgb G{0, 255, 0};
const Rgb B{0, 0, 255};
}  // namespace

TEST_CASE("split gives each kind of zone its own colour") {
    Calibrations calibrations;
    Painter painter({R, G, B}, Mode::Split, calibrations);
    // in device order: two RAM sticks, then the pump and four fans of one controller
    CHECK(painter.zone("VENGEANCE RGB", "DIMM", 10, Shape::Line) == std::vector<Rgb>(10, R));
    CHECK(painter.zone("VENGEANCE RGB", "DIMM", 10, Shape::Line) == std::vector<Rgb>(10, R));
    CHECK(painter.zone("COMMANDER Core", "H150i", 29, Shape::Ring) == std::vector<Rgb>(29, G));
    for (int fan = 0; fan < 4; ++fan) {
        CHECK(painter.zone("COMMANDER Core", "8-LED Series Fan", 8, Shape::Ring) == std::vector<Rgb>(8, B));
    }
    CHECK(painter.zone("Strip", "LED strip", 3, Shape::Ring) == std::vector<Rgb>(3, R));  // a fourth kind: starts over
}

TEST_CASE("single and arc") {
    Calibrations calibrations;
    Painter single({R, G, B}, Mode::Single, calibrations);
    CHECK(single.zone("x", "y", 4, Shape::Ring) == std::vector<Rgb>(4, R));
    Painter arc({R, G, B}, Mode::Arc, calibrations);
    std::vector<Rgb> ram = arc.zone("x", "y", 10, Shape::Line);
    CHECK(ram.front() == R);
    CHECK(ram.back() == B);
}

TEST_CASE("calibration gains are applied") {
    Calibrations calibrations;
    calibrations.entries.push_back(Calibration{"commander", "fan", {0.5, 1.0, 1.0}});
    Painter painter({WHITE}, Mode::Single, calibrations);
    CHECK(painter.zone("iCUE COMMANDER Core", "8-LED Series Fan", 1, Shape::Ring)[0] == Rgb{128, 255, 255});
    CHECK(painter.zone("iCUE COMMANDER Core", "H150i", 1, Shape::Ring)[0] == WHITE);
}
