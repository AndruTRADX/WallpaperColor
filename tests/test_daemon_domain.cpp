// The service's rules: which zone gets what, which drivers are needed, where a fade starts.
// Pure: no fakes needed.

#include <doctest/doctest.h>

#include "daemon/domain/driver_choice.hpp"
#include "daemon/domain/fade.hpp"
#include "daemon/domain/outputs.hpp"

using namespace wpc;

namespace {

ComponentSettings all_but(Component off) {
    ComponentSettings c;
    c.on[static_cast<size_t>(off)] = false;
    c.chosen = true;
    return c;
}

}  // namespace

TEST_CASE("only the zones of chosen components are painted") {
    Device aio{"d", "AIO", {Zone{"Pump", 4, Component::Pump, Shape::Ring}, Zone{"Fan", 8, Component::Fans, Shape::Ring}},
               std::vector<Rgb>(12, Rgb{1, 2, 3})};
    Calibrations none;
    Painter painter({Rgb{255, 0, 0}}, Mode::Single, none);
    std::vector<Output> outputs;
    std::vector<ipc::DeviceInfo> found;
    plan_outputs("cooler", {aio}, all_but(Component::Pump), painter, outputs, found);

    REQUIRE(outputs.size() == 1);
    CHECK(outputs[0].zones == std::vector<std::pair<size_t, size_t>>{{1, 8}});  // the fan only
    CHECK(outputs[0].target == std::vector<Rgb>(8, Rgb{255, 0, 0}));
    CHECK(outputs[0].reported.size() == 8);  // what the fan's LEDs show now
    CHECK(outputs[0].key == "cooler d");
    REQUIRE(found.size() == 1);
    CHECK(found[0].zones.size() == 2);  // every zone is listed, chosen or not
    CHECK(found[0].zones[0].component == Component::Pump);
}

TEST_CASE("a zone corrected by [[component]] follows its new component") {
    Device board{"b", "Board", {Zone{"Strip", 3, Component::Other, Shape::Line}}, {}};
    ComponentSettings c = all_but(Component::Other);
    c.rules.push_back(ComponentRule{"board", std::nullopt, Component::Argb});
    Calibrations none;
    Painter painter({WHITE}, Mode::Single, none);
    std::vector<Output> outputs;
    std::vector<ipc::DeviceInfo> found;
    plan_outputs("x", {board}, c, painter, outputs, found);
    CHECK(outputs.size() == 1);  // "other" is off, but the rule makes it argb
    CHECK(found[0].zones[0].component == Component::Argb);
}

TEST_CASE("which drivers the chosen components need") {
    ComponentSettings no_ram = all_but(Component::Ram);
    CHECK(driver_needed({Component::Ram, Component::Fans}, no_ram));
    CHECK_FALSE(driver_needed({Component::Ram}, no_ram));
    std::vector<Component> missing = unpainted({{Component::Fans}, {Component::Motherboard}}, no_ram);
    CHECK(std::find(missing.begin(), missing.end(), Component::Pump) != missing.end());
    CHECK(std::find(missing.begin(), missing.end(), Component::Fans) == missing.end());
    CHECK(std::find(missing.begin(), missing.end(), Component::Ram) == missing.end());  // not chosen
}

TEST_CASE("fades") {
    std::vector<Rgb> target(3, WHITE);
    std::vector<Rgb> sent(3, Rgb{1, 1, 1});
    std::vector<Rgb> reported(3, Rgb{2, 2, 2});
    CHECK(fade_start(&sent, reported, target) == sent);
    CHECK(fade_start(nullptr, reported, target) == reported);
    CHECK(fade_start(nullptr, {}, target) == target);  // unknown: no fade

    CHECK(fade_frames(0.6, true) == 15);
    CHECK(fade_frames(0.0, true) == 1);
    CHECK(fade_frames(5.0, true) == 38);  // at most 1.5 s
    CHECK(fade_frames(0.6, false) == 1);  // nothing changes
    CHECK(fade_frame(sent, target, 15, 15) == target);
    CHECK(fade_frame(sent, target, 7, 15)[0].r > 1);
}
