#include <doctest/doctest.h>

#include "settings/settings.hpp"
#include "settings/settings_file.hpp"
#include "utils/error.hpp"

using namespace wpc;

TEST_CASE("empty file gives defaults") {
    CHECK(parse_settings("") == Config{});
}

TEST_CASE("partial file keeps other defaults") {
    Config c = parse_settings("[extract]\nmin_saturation = 0.5\n");
    CHECK(c.extract.min_saturation == 0.5);
    CHECK(c.extract.palette_size == ExtractConfig{}.palette_size);
}

TEST_CASE("mode is arc by default and parses") {
    CHECK(Config{}.led.mode == Mode::Arc);
    CHECK(parse_settings("[led]\nmode = \"single\"\n").led.mode == Mode::Single);
    CHECK_THROWS_AS(parse_settings("[led]\nmode = \"rainbow\"\n"), Error);
}

TEST_CASE("calibration matches device and zone") {
    Config c = parse_settings(
        "[[calibration]]\ndevice = \"commander\"\nzone = \"fan\"\ngain = [0.72, 0.78, 1.0]\n\n"
        "[[calibration]]\ndevice = \"VENGEANCE\"\ngain = [1.0, 0.9, 0.9]\n");
    CHECK(c.calibration.gain("iCUE COMMANDER Core", "8-LED Series Fan") == std::array<float, 3>{0.72f, 0.78f, 1.0f});
    CHECK(c.calibration.gain("iCUE COMMANDER Core", "H150i ELITE CAPELLIX XT") == std::array<float, 3>{1, 1, 1});
    CHECK(c.calibration.gain("VENGEANCE RGB", "anything") == std::array<float, 3>{1.0f, 0.9f, 0.9f});
    CHECK(c.calibration.gain("Other", "fan") == std::array<float, 3>{1, 1, 1});
    CHECK(write_settings(Config{}).find("calibration") == std::string::npos);
    CHECK(parse_settings(write_settings(c)) == c);
}

TEST_CASE("unknown keys are rejected") {
    CHECK_THROWS_AS(parse_settings("[extract]\nmin_saturaton = 0.5\n"), Error);
}

TEST_CASE("round trip") {
    Config c;
    CHECK(parse_settings(write_settings(c)) == c);
}

TEST_CASE("missing file gives defaults") {
    CHECK(read_settings("/nonexistent/wallpapercolor.toml") == Config{});
}

TEST_CASE("wrong types are rejected") {
    CHECK_THROWS_AS(parse_settings("[extract]\npalette_size = 10.5\n"), Error);
    CHECK_THROWS_AS(parse_settings("[extract]\npalette_size = -1\n"), Error);
    CHECK_THROWS_AS(parse_settings("[[calibration]]\ndevice = \"x\"\ngain = [1, 1]\n"), Error);
    CHECK_THROWS_AS(parse_settings("[[calibration]]\ngain = [1, 1, 1]\n"), Error);
    CHECK_THROWS_AS(parse_settings("led = 3\n"), Error);
    CHECK(parse_settings("[extract]\nmin_saturation = 1\n").extract.min_saturation == 1.0);
}

TEST_CASE("split mode and fade") {
    Config c = parse_settings("[led]\nmode = \"split\"\nfade = 1\n");
    CHECK(c.led.mode == Mode::Split);
    CHECK(c.led.fade == 1.0);
    CHECK(Config{}.led.fade == 0.6);
    CHECK(parse_settings(write_settings(c)) == c);
}

TEST_CASE("components: all on unless [components] says otherwise") {
    for (Component c : ALL_COMPONENTS) CHECK(Config{}.components.wants(c));
    CHECK_FALSE(Config{}.components.chosen);

    Config c = parse_settings("[components]\nram = false\npump = true\n");
    CHECK(c.components.chosen);
    CHECK_FALSE(c.components.wants(Component::Ram));
    CHECK(c.components.wants(Component::Pump));
    CHECK(c.components.wants(Component::Fans));  // not written: on
    CHECK(parse_settings(write_settings(c)) == c);

    CHECK_THROWS_AS(parse_settings("[components]\nrams = false\n"), Error);
    CHECK_THROWS_AS(parse_settings("[components]\nram = \"no\"\n"), Error);
}

TEST_CASE("[motherboard] enabled = false still turns the board off without [components]") {
    Config old = parse_settings("[motherboard]\nenabled = false\n");
    CHECK_FALSE(old.components.wants(Component::Motherboard));
    CHECK_FALSE(old.components.wants(Component::Argb));
    CHECK(old.components.wants(Component::Fans));

    Config both = parse_settings("[motherboard]\nenabled = false\n[components]\nfans = true\n");
    CHECK(both.components.wants(Component::Motherboard));  // [components] decides
}

TEST_CASE("[[component]] corrects what a zone is") {
    Config c = parse_settings(
        "[[component]]\ndevice = \"commander\"\nzone = \"h150i\"\nis = \"pump\"\n\n"
        "[[component]]\ndevice = \"Strip\"\nis = \"argb\"\n");
    CHECK(c.components.component_of("iCUE COMMANDER Core", "H150i ELITE CAPELLIX XT", Component::Other) == Component::Pump);
    CHECK(c.components.component_of("iCUE COMMANDER Core", "8-LED Series Fan", Component::Fans) == Component::Fans);
    CHECK(c.components.component_of("LED Strip", "anything", Component::Strip) == Component::Argb);
    CHECK(parse_settings(write_settings(c)) == c);
    CHECK_THROWS_AS(parse_settings("[[component]]\ndevice = \"x\"\nis = \"lamp\"\n"), Error);
    CHECK_THROWS_AS(parse_settings("[[component]]\ndevice = \"x\"\n"), Error);
}

TEST_CASE("driver tables are kept for their driver") {
    Config c = parse_settings(
        "[motherboard]\nargb_order = \"RGB\"\nargb_header1 = [8, 8]\n\n"
        "[openrgb]\naddress = \"127.0.0.1:6742\"\n\n[acmek1]\nper_key = true\nspeed = 1.5\n");
    DriverSettings board = c.drivers.of("motherboard");
    CHECK(board.string("argb_order") == "RGB");
    CHECK(board.numbers("argb_header1") == std::vector<double>{8, 8});
    CHECK(c.drivers.of("acmek1").boolean("per_key") == true);
    CHECK(c.drivers.of("acmek1").number("speed") == 1.5);
    CHECK(c.drivers.of("nothing").values().empty());
    CHECK_THROWS_AS(board.number("argb_order"), Error);  // a string, not a number

    std::string text = write_settings(c);
    CHECK(text.find("argb_header1 = [8, 8]") != std::string::npos);
    CHECK(parse_settings(text) == c);

    CHECK_THROWS_AS(parse_settings("[openrgb]\naddress = { host = \"x\" }\n"), Error);
    CHECK_THROWS_AS(parse_settings("[openrgb]\nnames = [\"a\"]\n"), Error);
}

TEST_CASE("numbers out of their range are mistakes, so the service never uses them") {
    // What made the programs abort (a picture resized to 2e9 × 2e9) or do undefined maths (NaN).
    CHECK_THROWS_WITH_AS(parse_settings("[extract]\nsample_size = 2000000000\n"),
                         doctest::Contains("expected a whole number from 1 to 1024"), Error);
    CHECK_THROWS_AS(parse_settings("[extract]\nsample_size = 0\n"), Error);
    CHECK_THROWS_AS(parse_settings("[extract]\npalette_size = 1000\n"), Error);
    CHECK_THROWS_WITH_AS(parse_settings("[led]\nfade = nan\n"), doctest::Contains("expected a finite number"), Error);
    CHECK_THROWS_AS(parse_settings("[led]\nfade = inf\n"), Error);
    CHECK_THROWS_AS(parse_settings("[led]\nfade = -0.5\n"), Error);
    CHECK_THROWS_AS(parse_settings("[extract]\nmin_saturation = 1.5\n"), Error);
    CHECK_THROWS_AS(parse_settings("[extract]\nmin_share = -0.1\n"), Error);
    CHECK_THROWS_AS(parse_settings("[[calibration]]\ndevice = \"x\"\ngain = [1, nan, 1]\n"), Error);
    CHECK_THROWS_AS(parse_settings("[motherboard]\nargb_header1 = [8, inf]\n"), Error);

    // The edges are fine; a longer fade is the longest one (as the README says: at most 1.5).
    CHECK(parse_settings("[extract]\nsample_size = 1024\npalette_size = 64\n").extract.sample_size == 1024);
    CHECK(parse_settings("[led]\nfade = 9\n").led.fade == ranges::FADE.max);
    CHECK(parse_settings("[led]\nfade = 0\n").led.fade == 0.0);
}

TEST_CASE("the picture's side for a sample size never wraps round") {
    ExtractConfig x;
    x.sample_size = UINT32_MAX;
    CHECK(picture_side(x) == UINT32_MAX);
    x.sample_size = 96;
    CHECK(picture_side(x) == 384);
}
