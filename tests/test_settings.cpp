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
    CHECK(s.set_float("extract", "min_saturation", 0.7, 2));
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
    CHECK(s.set_float("extract", "min_saturation", 0.333, 2));
    Config c = s.config();
    CHECK(c.led.mode == Mode::Arc);
    CHECK(c.extract.min_saturation == 0.33);
    CHECK(s.text() == "[led]\nmode = \"arc\"\n\n[extract]\nmin_saturation = 0.33\n");
}

TEST_CASE("values are clamped") {
    Settings s = Settings::from_text(USER);
    s.set_gain(0, 2, 1.7);
    s.set_gain(7, 0, 0.1);  // no such entry: ignored
    Config c = s.config();
    CHECK(c.calibration[0].gain[2] == 1.0);
    CHECK(c.calibration.size() == 1);
}

TEST_CASE("missing keys are added to their table") {
    Settings s = Settings::from_text("[led]\n# nothing yet\n\n[extract]\npalette_size = 8\n");
    CHECK(s.set_mode(Mode::Single));
    CHECK(s.set_float("extract", "min_saturation", 0.5, 2));
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

TEST_CASE("numbers are rounded and integers stay integers") {
    Settings s = Settings::from_text("");
    CHECK(s.set_float("led", "fade", 0.1 + 0.2, 1));
    CHECK(s.set_integer("extract", "palette_size", 12));
    CHECK(s.text() == "[led]\nfade = 0.3\n\n[extract]\npalette_size = 12\n");
    CHECK(s.config().extract.palette_size == 12);
}

TEST_CASE("calibration entries can be added and removed") {
    Settings s = Settings::from_text(USER);
    CHECK(s.add_calibration("VENGEANCE RGB", std::nullopt));
    CHECK(s.add_calibration("iCUE COMMANDER Core", "H150i"));
    Config c = s.config();
    REQUIRE(c.calibration.size() == 3);
    CHECK(c.calibration[1].device == "VENGEANCE RGB");
    CHECK_FALSE(c.calibration[1].zone);
    CHECK(c.calibration[2].zone == "H150i");
    CHECK(c.calibration[2].gain == std::array<double, 3>{1.0, 1.0, 1.0});

    CHECK(s.remove_calibration(0));  // the first one, with its "# fans have a weak blue"
    std::string text = s.text();
    CHECK(text.find("weak blue") == std::string::npos);
    CHECK(text.find("# intensity") != std::string::npos);
    c = s.config();
    REQUIRE(c.calibration.size() == 2);
    CHECK(c.calibration[0].device == "VENGEANCE RGB");
    CHECK(text.find("\n\n\n") == std::string::npos);

    CHECK(s.remove_calibration(5));  // no such entry
    CHECK(s.config().calibration.size() == 2);
    CHECK_FALSE(Settings::from_text("calibration = []\n").add_calibration("x", std::nullopt));
}
