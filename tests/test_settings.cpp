#include <doctest/doctest.h>

#include "client/settings.hpp"

using namespace wpc;

namespace {
const char* const USER =
    "# my settings\n[led]\nmode = \"arc\"  # gradient\n\n[extract]\nmin_saturation = 0.65   # intensity\n\n"
    "# fans have a weak blue\n[[calibration]]\ndevice = \"COMMANDER Core\"\nzone = \"Fan\"\n"
    "gain = [0.5, 0.5 ,1.0]    # red, green, blue\n";
}

TEST_CASE("edits keep comments") {
    Settings s = Settings::from_text(USER);
    CHECK(s.set_mode(Mode::Single));
    CHECK(s.set_intensity(0.7));
    CHECK(s.set_gain(0, 0, 0.55));
    std::string out = s.text();
    for (const char* comment : {"# my settings", "# gradient", "# intensity", "# fans have a weak blue", "# red, green, blue"}) {
        CAPTURE(comment);
        CHECK(out.find(comment) != std::string::npos);
    }
    Config c = s.config();
    CHECK(c.led.mode == Mode::Single);
    CHECK(c.extract.min_saturation == 0.7);
    CHECK(c.calibration[0].gain == std::array<double, 3>{0.55, 0.5, 1.0});
}

TEST_CASE("empty file gets the tables") {
    Settings s = Settings::from_text("");
    CHECK(s.set_mode(Mode::Arc));
    CHECK(s.set_intensity(0.333));
    Config c = s.config();
    CHECK(c.led.mode == Mode::Arc);
    CHECK(c.extract.min_saturation == 0.33);
    CHECK(s.text() == "[led]\nmode = \"arc\"\n\n[extract]\nmin_saturation = 0.33\n");
}

TEST_CASE("values are clamped") {
    Settings s = Settings::from_text(USER);
    s.set_intensity(5.0);
    s.set_gain(0, 2, 1.7);
    s.set_gain(7, 0, 0.1);  // no such entry: ignored
    Config c = s.config();
    CHECK(c.extract.min_saturation == 1.0);
    CHECK(c.calibration[0].gain[2] == 1.0);
    CHECK(c.calibration.size() == 1);
}

TEST_CASE("missing keys are added to their table") {
    Settings s = Settings::from_text("[led]\n# nothing yet\n\n[extract]\npalette_size = 8\n");
    CHECK(s.set_mode(Mode::Single));
    CHECK(s.set_intensity(0.5));
    CHECK(s.text() == "[led]\nmode = \"single\"\n# nothing yet\n\n[extract]\npalette_size = 8\nmin_saturation = 0.5\n");
}

TEST_CASE("unusual layouts are left alone") {
    Settings dotted = Settings::from_text("led.mode = \"arc\"\n");
    CHECK_FALSE(dotted.set_mode(Mode::Single));
    CHECK(dotted.text() == "led.mode = \"arc\"\n");
    Settings inline_table = Settings::from_text("led = { mode = \"arc\" }\n");
    CHECK_FALSE(inline_table.set_mode(Mode::Single));
}

TEST_CASE("a gain array over several lines, as `config` prints it") {
    Settings s = Settings::from_text("[[calibration]]\ndevice = \"x\"\ngain = [\n    0.72,\n    0.78, # g\n    1.0,\n]  # rgb\nzone = \"Fan\"\n");
    CHECK(s.set_gain(0, 0, 0.5));
    CHECK(s.text() == "[[calibration]]\ndevice = \"x\"\ngain = [0.5, 0.78, 1.0]  # rgb\nzone = \"Fan\"\n");
    CHECK(s.config().calibration[0].zone == "Fan");
}
