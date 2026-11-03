#include <doctest/doctest.h>

#include "shared/infrastructure/services/settings_file.hpp"
#include "terminal/infrastructure/services/settings_editor.hpp"

using namespace wpc;

namespace {

/// The settings the editor's text gives.
Config config_of(const TomlSettingsEditor& editor) {
    return SettingsFile().parse(editor.text());
}

const char* const USER =
    "# my settings\n[led]\nmode = \"arc\"  # gradient\n\n[extract]\nmin_saturation = 0.65   # intensity\n\n"
    "# fans have a weak blue\n[[calibration]]\ndevice = \"COMMANDER Core\"\nzone = \"Fan\"\n"
    "gain = [0.5, 0.5 ,1.0]    # red, green, blue\n";
}

TEST_CASE("edits keep comments") {
    TomlSettingsEditor s = TomlSettingsEditor::from_text(USER);
    CHECK(s.set_mode(Mode::Single));
    CHECK(s.set_float("extract", "min_saturation", 0.7, 2));
    CHECK(s.set_gain(0, 0, 0.55));
    std::string out = s.text();
    for (const char* comment : {"# my settings", "# gradient", "# intensity", "# fans have a weak blue", "# red, green, blue"}) {
        CAPTURE(comment);
        CHECK(out.find(comment) != std::string::npos);
    }
    Config c = config_of(s);
    CHECK(c.led.mode == Mode::Single);
    CHECK(c.extract.min_saturation == 0.7);
    CHECK(c.calibration.entries[0].gain == std::array<double, 3>{0.55, 0.5, 1.0});
}

TEST_CASE("empty file gets the tables") {
    TomlSettingsEditor s = TomlSettingsEditor::from_text("");
    CHECK(s.set_mode(Mode::Arc));
    CHECK(s.set_float("extract", "min_saturation", 0.333, 2));
    Config c = config_of(s);
    CHECK(c.led.mode == Mode::Arc);
    CHECK(c.extract.min_saturation == 0.33);
    CHECK(s.text() == "[led]\nmode = \"arc\"\n\n[extract]\nmin_saturation = 0.33\n");
}

TEST_CASE("values are clamped") {
    TomlSettingsEditor s = TomlSettingsEditor::from_text(USER);
    s.set_gain(0, 2, 1.7);
    s.set_gain(7, 0, 0.1);  // no such entry: ignored
    Config c = config_of(s);
    CHECK(c.calibration.entries[0].gain[2] == 1.0);
    CHECK(c.calibration.entries.size() == 1);
}

TEST_CASE("missing keys are added to their table") {
    TomlSettingsEditor s = TomlSettingsEditor::from_text("[led]\n# nothing yet\n\n[extract]\npalette_size = 8\n");
    CHECK(s.set_mode(Mode::Single));
    CHECK(s.set_float("extract", "min_saturation", 0.5, 2));
    CHECK(s.text() == "[led]\nmode = \"single\"\n# nothing yet\n\n[extract]\npalette_size = 8\nmin_saturation = 0.5\n");
}

TEST_CASE("unusual layouts are left alone") {
    TomlSettingsEditor dotted = TomlSettingsEditor::from_text("led.mode = \"arc\"\n");
    CHECK_FALSE(dotted.set_mode(Mode::Single));
    CHECK(dotted.text() == "led.mode = \"arc\"\n");
    TomlSettingsEditor inline_table = TomlSettingsEditor::from_text("led = { mode = \"arc\" }\n");
    CHECK_FALSE(inline_table.set_mode(Mode::Single));
}

TEST_CASE("a gain array over several lines, as `config` prints it") {
    TomlSettingsEditor s = TomlSettingsEditor::from_text("[[calibration]]\ndevice = \"x\"\ngain = [\n    0.72,\n    0.78, # g\n    1.0,\n]  # rgb\nzone = \"Fan\"\n");
    CHECK(s.set_gain(0, 0, 0.5));
    CHECK(s.text() == "[[calibration]]\ndevice = \"x\"\ngain = [0.5, 0.78, 1.0]  # rgb\nzone = \"Fan\"\n");
    CHECK(config_of(s).calibration.entries[0].zone == "Fan");
}

TEST_CASE("numbers are rounded and integers stay integers") {
    TomlSettingsEditor s = TomlSettingsEditor::from_text("");
    CHECK(s.set_float("led", "fade", 0.1 + 0.2, 1));
    CHECK(s.set_integer("extract", "palette_size", 12));
    CHECK(s.text() == "[led]\nfade = 0.3\n\n[extract]\npalette_size = 12\n");
    CHECK(config_of(s).extract.palette_size == 12);
}

TEST_CASE("calibration entries can be added and removed") {
    TomlSettingsEditor s = TomlSettingsEditor::from_text(USER);
    CHECK(s.add_calibration("VENGEANCE RGB", std::nullopt));
    CHECK(s.add_calibration("iCUE COMMANDER Core", "H150i"));
    Config c = config_of(s);
    REQUIRE(c.calibration.entries.size() == 3);
    CHECK(c.calibration.entries[1].device == "VENGEANCE RGB");
    CHECK_FALSE(c.calibration.entries[1].zone);
    CHECK(c.calibration.entries[2].zone == "H150i");
    CHECK(c.calibration.entries[2].gain == std::array<double, 3>{1.0, 1.0, 1.0});

    CHECK(s.remove_calibration(0));  // the first one, with its "# fans have a weak blue"
    std::string text = s.text();
    CHECK(text.find("weak blue") == std::string::npos);
    CHECK(text.find("# intensity") != std::string::npos);
    c = config_of(s);
    REQUIRE(c.calibration.entries.size() == 2);
    CHECK(c.calibration.entries[0].device == "VENGEANCE RGB");
    CHECK(text.find("\n\n\n") == std::string::npos);

    CHECK(s.remove_calibration(5));  // no such entry
    CHECK(config_of(s).calibration.entries.size() == 2);
    CHECK_FALSE(TomlSettingsEditor::from_text("calibration = []\n").add_calibration("x", std::nullopt));
}

TEST_CASE("components are written as a whole table") {
    TomlSettingsEditor s = TomlSettingsEditor::from_text("[motherboard]\nenabled = false   # old switch\nargb_header1 = [8, 8]\n");
    std::array<bool, ALL_COMPONENTS.size()> chosen = config_of(s).components.on;
    CHECK_FALSE(chosen[static_cast<size_t>(Component::Argb)]);  // from the old switch
    chosen[static_cast<size_t>(Component::Ram)] = false;
    CHECK(s.set_components(chosen));

    Config c = config_of(s);
    CHECK(c.components.chosen);
    CHECK_FALSE(c.components.wants(Component::Ram));
    CHECK_FALSE(c.components.wants(Component::Motherboard));  // kept off, now written in [components]
    CHECK(c.components.wants(Component::Fans));
    CHECK(s.text().find("# old switch") != std::string::npos);
    CHECK(s.text().find("[components]\nfans = true\npump = true\nram = false\n") != std::string::npos);
}

TEST_CASE("a `calibration` key in another table doesn't stop editing the calibrations") {
    TomlSettingsEditor s = TomlSettingsEditor::from_text(
        "[openrgb]\ncalibration = 1\n\n[[calibration]]\ndevice = \"x\"\ngain = [1.0, 1.0, 1.0]\n");
    CHECK(s.set_gain(0, 2, 0.5));  // as add_calibration and remove_calibration
    CHECK(s.text().find("gain = [1.0, 1.0, 0.5]") != std::string::npos);
    CHECK_FALSE(TomlSettingsEditor::from_text("calibration = [{ device = \"x\" }]\n").set_gain(0, 0, 0.5));
}
