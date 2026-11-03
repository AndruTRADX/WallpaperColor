// The driver modules as both programs reach them (drivers/host behind each program's
// DriverModules), with the test module (fake_driver.cpp): loaded and unloaded by name, and
// its errors arrive as wpc::Error.

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "daemon/infrastructure/external/driver_modules.hpp"
#include "terminal/infrastructure/external/driver_modules.hpp"
#include "utils/error.hpp"

namespace {

bool mapped(std::string_view name) {
    std::ifstream maps("/proc/self/maps");
    std::stringstream text;
    text << maps.rdbuf();
    return text.str().find(name) != std::string::npos;
}

template <class Modules>
void check_modules() {
    using namespace wpc;
    setenv("WPC_DRIVER_DIR", WPC_TEST_MODULES, 1);
    Modules modules;
    std::vector<std::string> problems;
    auto installed = modules.installed(problems);
    REQUIRE(installed.size() == 1);
    CHECK(installed[0].name == "fake");
    CHECK_FALSE(mapped("wpc-driver-fake.so"));

    modules.load("fake", DriverSettings("fake", {{"zones", std::vector<double>{2}}}));
    CHECK(mapped("wpc-driver-fake.so"));
    std::vector<Device> devices = modules.devices("fake");
    REQUIRE(devices.size() == 1);
    CHECK(devices[0].zones.size() == 1);

    std::vector<Rgb> white(2, WHITE);
    ZoneFrame frame{0, white};
    try {
        modules.show("fake", 0, std::span(&frame, 1));
        FAIL("no error");
    } catch (const Error& e) {
        CHECK(std::string(e.what()) == "device 0: zone 0 255");
    }

    modules.unload("fake");
    CHECK_FALSE(mapped("wpc-driver-fake.so"));
    CHECK_THROWS_AS(modules.load("missing", DriverSettings("missing", {})), Error);
    unsetenv("WPC_DRIVER_DIR");
}

}  // namespace

TEST_CASE("the service's driver modules") {
    check_modules<wpc::daemon::HostDriverModules>();
}

TEST_CASE("the terminal's driver modules") {
    check_modules<wpc::terminal::HostDriverModules>();
}
