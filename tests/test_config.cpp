#include <doctest/doctest.h>

#include "shared/domain/settings/settings.hpp"
#include "shared/infrastructure/services/settings_file.hpp"
#include "utils/error.hpp"

using namespace wpc;

TEST_CASE("empty file gives defaults") {
    CHECK(SettingsFile().parse("") == Config{});
}

TEST_CASE("partial file keeps other defaults") {
    Config c = SettingsFile().parse("[extract]\nmin_saturation = 0.5\n");
    CHECK(c.extract.min_saturation == 0.5);
    CHECK(c.extract.palette_size == ExtractConfig{}.palette_size);
}

TEST_CASE("mode is arc by default and parses") {
    CHECK(Config{}.led.mode == Mode::Arc);
    CHECK(SettingsFile().parse("[led]\nmode = \"single\"\n").led.mode == Mode::Single);
    CHECK_THROWS_AS(SettingsFile().parse("[led]\nmode = \"rainbow\"\n"), Error);
}

TEST_CASE("calibration matches device and zone") {
    Config c = SettingsFile().parse(
        "[[calibration]]\ndevice = \"commander\"\nzone = \"fan\"\ngain = [0.72, 0.78, 1.0]\n\n"
        "[[calibration]]\ndevice = \"VENGEANCE\"\ngain = [1.0, 0.9, 0.9]\n");
    CHECK(c.calibration.gain("iCUE COMMANDER Core", "8-LED Series Fan") == std::array<float, 3>{0.72f, 0.78f, 1.0f});
    CHECK(c.calibration.gain("iCUE COMMANDER Core", "H150i ELITE CAPELLIX XT") == std::array<float, 3>{1, 1, 1});
    CHECK(c.calibration.gain("VENGEANCE RGB", "anything") == std::array<float, 3>{1.0f, 0.9f, 0.9f});
    CHECK(c.calibration.gain("Other", "fan") == std::array<float, 3>{1, 1, 1});
    CHECK(SettingsFile().write(Config{}).find("calibration") == std::string::npos);
    CHECK(SettingsFile().parse(SettingsFile().write(c)) == c);
}

TEST_CASE("unknown keys are rejected") {
    CHECK_THROWS_AS(SettingsFile().parse("[extract]\nmin_saturaton = 0.5\n"), Error);
}

TEST_CASE("round trip") {
    Config c;
    CHECK(SettingsFile().parse(SettingsFile().write(c)) == c);
}

TEST_CASE("missing file gives defaults") {
    CHECK(SettingsFile().read("/nonexistent/wallpapercolor.toml") == Config{});
}

TEST_CASE("wrong types are rejected") {
    CHECK_THROWS_AS(SettingsFile().parse("[extract]\npalette_size = 10.5\n"), Error);
    CHECK_THROWS_AS(SettingsFile().parse("[extract]\npalette_size = -1\n"), Error);
    CHECK_THROWS_AS(SettingsFile().parse("[[calibration]]\ndevice = \"x\"\ngain = [1, 1]\n"), Error);
    CHECK_THROWS_AS(SettingsFile().parse("[[calibration]]\ngain = [1, 1, 1]\n"), Error);
    CHECK_THROWS_AS(SettingsFile().parse("led = 3\n"), Error);
    CHECK(SettingsFile().parse("[extract]\nmin_saturation = 1\n").extract.min_saturation == 1.0);
}

TEST_CASE("split mode and fade") {
    Config c = SettingsFile().parse("[led]\nmode = \"split\"\nfade = 1\n");
    CHECK(c.led.mode == Mode::Split);
    CHECK(c.led.fade == 1.0);
    CHECK(Config{}.led.fade == 0.6);
    CHECK(SettingsFile().parse(SettingsFile().write(c)) == c);
}

TEST_CASE("components: all on unless [components] says otherwise") {
    for (Component c : ALL_COMPONENTS) CHECK(Config{}.components.wants(c));
    CHECK_FALSE(Config{}.components.chosen);

    Config c = SettingsFile().parse("[components]\nram = false\npump = true\n");
    CHECK(c.components.chosen);
    CHECK_FALSE(c.components.wants(Component::Ram));
    CHECK(c.components.wants(Component::Pump));
    CHECK(c.components.wants(Component::Fans));  // not written: on
    CHECK(SettingsFile().parse(SettingsFile().write(c)) == c);

    CHECK_THROWS_AS(SettingsFile().parse("[components]\nrams = false\n"), Error);
    CHECK_THROWS_AS(SettingsFile().parse("[components]\nram = \"no\"\n"), Error);
}

TEST_CASE("[motherboard] enabled = false still turns the board off without [components]") {
    Config old = SettingsFile().parse("[motherboard]\nenabled = false\n");
    CHECK_FALSE(old.components.wants(Component::Motherboard));
    CHECK_FALSE(old.components.wants(Component::Argb));
    CHECK(old.components.wants(Component::Fans));

    Config both = SettingsFile().parse("[motherboard]\nenabled = false\n[components]\nfans = true\n");
    CHECK(both.components.wants(Component::Motherboard));  // [components] decides
}

TEST_CASE("[[component]] corrects what a zone is") {
    Config c = SettingsFile().parse(
        "[[component]]\ndevice = \"commander\"\nzone = \"h150i\"\nis = \"pump\"\n\n"
        "[[component]]\ndevice = \"Strip\"\nis = \"argb\"\n");
    CHECK(c.components.component_of("iCUE COMMANDER Core", "H150i ELITE CAPELLIX XT", Component::Other) == Component::Pump);
    CHECK(c.components.component_of("iCUE COMMANDER Core", "8-LED Series Fan", Component::Fans) == Component::Fans);
    CHECK(c.components.component_of("LED Strip", "anything", Component::Strip) == Component::Argb);
    CHECK(SettingsFile().parse(SettingsFile().write(c)) == c);
    CHECK_THROWS_AS(SettingsFile().parse("[[component]]\ndevice = \"x\"\nis = \"lamp\"\n"), Error);
    CHECK_THROWS_AS(SettingsFile().parse("[[component]]\ndevice = \"x\"\n"), Error);
}

TEST_CASE("driver tables are kept for their driver") {
    Config c = SettingsFile().parse(
        "[motherboard]\nargb_order = \"RGB\"\nargb_header1 = [8, 8]\n\n"
        "[openrgb]\naddress = \"127.0.0.1:6742\"\n\n[acmek1]\nper_key = true\nspeed = 1.5\n");
    DriverSettings board = c.drivers.of("motherboard");
    CHECK(board.string("argb_order") == "RGB");
    CHECK(board.numbers("argb_header1") == std::vector<double>{8, 8});
    CHECK(c.drivers.of("acmek1").boolean("per_key") == true);
    CHECK(c.drivers.of("acmek1").number("speed") == 1.5);
    CHECK(c.drivers.of("nothing").values().empty());
    CHECK_THROWS_AS(board.number("argb_order"), Error);  // a string, not a number

    std::string text = SettingsFile().write(c);
    CHECK(text.find("argb_header1 = [8, 8]") != std::string::npos);
    CHECK(SettingsFile().parse(text) == c);

    CHECK_THROWS_AS(SettingsFile().parse("[openrgb]\naddress = { host = \"x\" }\n"), Error);
    CHECK_THROWS_AS(SettingsFile().parse("[openrgb]\nnames = [\"a\"]\n"), Error);
}
