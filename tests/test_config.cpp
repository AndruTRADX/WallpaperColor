#include <doctest/doctest.h>

#include "core/config.hpp"
#include "util/error.hpp"

using namespace wpc;

TEST_CASE("empty file gives defaults") {
    CHECK(Config::from_toml("") == Config{});
}

TEST_CASE("partial file keeps other defaults") {
    Config c = Config::from_toml("[extract]\nmin_saturation = 0.5\n");
    CHECK(c.extract.min_saturation == 0.5);
    CHECK(c.extract.palette_size == ExtractConfig{}.palette_size);
}

TEST_CASE("mode is arc by default and parses") {
    CHECK(Config{}.led.mode == Mode::Arc);
    CHECK(Config::from_toml("[led]\nmode = \"single\"\n").led.mode == Mode::Single);
    CHECK_THROWS_AS(Config::from_toml("[led]\nmode = \"rainbow\"\n"), Error);
}

TEST_CASE("calibration matches device and zone") {
    Config c = Config::from_toml(
        "[[calibration]]\ndevice = \"commander\"\nzone = \"fan\"\ngain = [0.72, 0.78, 1.0]\n\n"
        "[[calibration]]\ndevice = \"VENGEANCE\"\ngain = [1.0, 0.9, 0.9]\n");
    CHECK(c.gain("iCUE COMMANDER Core", "8-LED Series Fan") == std::array<float, 3>{0.72f, 0.78f, 1.0f});
    CHECK(c.gain("iCUE COMMANDER Core", "H150i ELITE CAPELLIX XT") == std::array<float, 3>{1, 1, 1});
    CHECK(c.gain("VENGEANCE RGB", "anything") == std::array<float, 3>{1.0f, 0.9f, 0.9f});
    CHECK(c.gain("Other", "fan") == std::array<float, 3>{1, 1, 1});
    CHECK(Config{}.to_toml().find("calibration") == std::string::npos);
    CHECK(Config::from_toml(c.to_toml()) == c);
}

TEST_CASE("unknown keys are rejected") {
    CHECK_THROWS_AS(Config::from_toml("[extract]\nmin_saturaton = 0.5\n"), Error);
}

TEST_CASE("round trip") {
    Config c;
    CHECK(Config::from_toml(c.to_toml()) == c);
}

TEST_CASE("missing file gives defaults") {
    CHECK(Config::load("/nonexistent/wallpapercolor.toml") == Config{});
}

TEST_CASE("wrong types are rejected") {
    CHECK_THROWS_AS(Config::from_toml("[extract]\npalette_size = 10.5\n"), Error);
    CHECK_THROWS_AS(Config::from_toml("[extract]\npalette_size = -1\n"), Error);
    CHECK_THROWS_AS(Config::from_toml("[[calibration]]\ndevice = \"x\"\ngain = [1, 1]\n"), Error);
    CHECK_THROWS_AS(Config::from_toml("[[calibration]]\ngain = [1, 1, 1]\n"), Error);
    CHECK_THROWS_AS(Config::from_toml("led = 3\n"), Error);
    CHECK(Config::from_toml("[extract]\nmin_saturation = 1\n").extract.min_saturation == 1.0);
}

TEST_CASE("split mode and fade") {
    Config c = Config::from_toml("[led]\nmode = \"split\"\nfade = 1\n");
    CHECK(c.led.mode == Mode::Split);
    CHECK(c.led.fade == 1.0);
    CHECK(Config{}.led.fade == 0.6);
    CHECK(Config::from_toml(c.to_toml()) == c);
}
