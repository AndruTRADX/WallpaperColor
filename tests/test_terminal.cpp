// The terminal's features, with fakes for the service's socket and the driver modules.

#include <doctest/doctest.h>

#include <format>

#include "terminal/application/features/choose_components/choose_components.hpp"
#include "terminal/application/features/list_devices/list_devices.hpp"
#include "terminal/application/features/preview_devices/preview_devices.hpp"
#include "terminal/application/features/save_settings/save_settings.hpp"
#include "terminal/application/features/service_status/service_status.hpp"
#include "terminal/application/features/set_colours/set_colours.hpp"
#include "shared/infrastructure/services/protocol_json.hpp"
#include "terminal/infrastructure/external/message_codec.hpp"
#include "terminal/infrastructure/external/settings_file.hpp"
#include "utils/error.hpp"

using namespace wpc;

namespace {

/// A strip driver and a board driver; the board isn't there.
struct FakeDrivers final : DriverModules {
    std::vector<std::string> in_memory;
    std::vector<std::string> shown;

    std::vector<DriverModule> installed(std::vector<std::string>&) override {
        return {DriverModule{"strip", "strip", {Component::Strip}}, DriverModule{"board", "board", {Component::Motherboard}}};
    }
    void load(const std::string& name, const DriverSettings&) override {
        if (name == "board") throw Error("no board");
        in_memory.push_back(name);
    }
    void unload(const std::string& name) override { std::erase(in_memory, name); }
    std::vector<Device> devices(const std::string&) override {
        return {Device{"s", "Strip", {Zone{"Top", 3, Component::Strip, Shape::Line}, Zone{"Fan", 4, Component::Fans, Shape::Ring}}, {}}};
    }
    void show(const std::string& name, size_t, std::span<const ZoneFrame> zones) override {
        for (const ZoneFrame& z : zones) shown.push_back(std::format("{} zone {} {}", name, z.zone, z.colours[0].hex()));
    }
    std::string location() override { return "/fake"; }
};

struct NoPictures final : Pictures {
    Picture read(const std::filesystem::path&, uint32_t) override { throw Error("no pictures here"); }
};

/// Answers every request with a status.
struct FakeService final : ServiceSocket {
    std::string exchange(const std::string&) override {
        ipc::Status s;
        s.stops = {"#ff0000", "not a colour", "#0000ff"};
        return ProtocolJson().encode(ipc::Response::of(s));
    }
};

Config fans_off() {
    Config c;
    c.components.on[static_cast<size_t>(Component::Fans)] = false;
    c.components.chosen = true;
    return c;
}

}  // namespace

TEST_CASE("devices: every driver is loaded, asked and unloaded") {
    FakeDrivers drivers;
    DeviceList list = list_devices(drivers, fans_off());
    REQUIRE(list.drivers.size() == 2);
    CHECK(list.drivers[1].error == "no board");
    REQUIRE(list.drivers[0].devices.size() == 1);
    CHECK(list.drivers[0].devices[0].zones[0].chosen);
    CHECK_FALSE(list.drivers[0].devices[0].zones[1].chosen);  // fans are off
    CHECK(drivers.in_memory.empty());
}

TEST_CASE("set paints only the chosen zones and says what wasn't painted") {
    FakeDrivers drivers;
    NoPictures pictures;
    SetResult r = set_colours(drivers, pictures, fans_off(), ColoursToSet{{"#00ff00"}, std::nullopt, Mode::Single});
    CHECK(drivers.shown == std::vector<std::string>{"strip zone 0 #00ff00"});
    CHECK(r.painted == std::vector<std::string>{"Strip"});
    CHECK(r.not_painted == std::vector<std::string>{"board: no board"});
    CHECK(drivers.in_memory.empty());
    CHECK_THROWS_AS(set_colours(drivers, pictures, Config{}, ColoursToSet{{}, "/a.png", std::nullopt}), Error);
}

TEST_CASE("status gives the colours as colours") {
    FakeService service;
    ProtocolJsonCodec codec;
    ServiceStatus s = service_status(service, codec);
    CHECK(s.stops == std::vector<Rgb>{Rgb{255, 0, 0}, Rgb{0, 0, 255}});
}

TEST_CASE("the Drivers page's states come with the choices") {
    ipc::Status status;
    status.drivers = {ipc::DriverState{"strip", {Component::Strip}, true, std::nullopt},
                      ipc::DriverState{"board", {Component::Motherboard}, false, std::string("no board")}};
    std::vector<DriverModule> installed = {DriverModule{"strip", "strip", {Component::Strip}},
                                           DriverModule{"board", "board", {Component::Motherboard}}};
    std::vector<ComponentChoice> choices = component_choices(installed, Config{}, &status);
    const ComponentChoice& strip = choices[static_cast<size_t>(Component::Strip)];
    const ComponentChoice& board = choices[static_cast<size_t>(Component::Motherboard)];
    CHECK(strip.loaded);
    CHECK(board.error == "no board");
}

TEST_CASE("the preview paints what the service found, white during the test") {
    std::vector<ipc::DeviceInfo> devices = {ipc::DeviceInfo{"Strip", "strip", {ipc::ZoneInfo{"Top", 3, Component::Strip, Shape::Line},
                                                                           ipc::ZoneInfo{"Fan", 4, Component::Fans, Shape::Ring}}}};
    std::vector<PreviewDevice> preview = preview_devices(devices, {Rgb{255, 0, 0}}, true, fans_off());
    REQUIRE(preview.size() == 1);
    CHECK(preview[0].zones[0].leds == std::vector<Rgb>(3, WHITE));
    CHECK_FALSE(preview[0].zones[1].chosen);
    CHECK(preview[0].zones[1].leds.empty());
}

namespace {

/// An editor over a text, saved nowhere.
struct TextEditor final : SettingsEditor {
    std::string body;
    std::string text() const override { return body; }
    bool set_mode(Mode) override { return true; }
    bool set_components(const std::array<bool, ALL_COMPONENTS.size()>&) override { return true; }
    bool set_float(std::string_view, std::string_view, double, int) override { return true; }
    bool set_integer(std::string_view, std::string_view, long) override { return true; }
    bool set_gain(size_t, int, double) override { return true; }
    bool add_calibration(std::string_view, std::optional<std::string_view>) override { return true; }
    bool remove_calibration(size_t) override { return true; }
    void save() const override {}
};

}  // namespace

TEST_CASE("saving says when the colours must be taken from the picture again") {
    TomlSettingsFormat format;
    TextEditor editor;
    editor.body = "[led]\nfade = 1.0\n";
    CHECK_FALSE(save_settings(editor, format, Config{}).extract_changed);  // only the fade changed
    editor.body = "[extract]\nmin_saturation = 0.3\n";
    CHECK(save_settings(editor, format, Config{}).extract_changed);
}
