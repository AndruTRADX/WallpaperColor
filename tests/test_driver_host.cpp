// The driver modules as both programs reach them (drivers/host), with the test modules
// (fake_driver.cpp) and the real ones: listed, loaded and unloaded by name, a module from
// another build refused, and every error a module throws arriving as a wpc::Error that
// outlives the module.

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>

#include <unistd.h>

#include "drivers/host/driver_host.hpp"
#include "utils/error.hpp"

using namespace wpc;
namespace fs = std::filesystem;

namespace {

/// Whether a file named `name` is mapped into this process now.
bool mapped(std::string_view name) {
    std::ifstream maps("/proc/self/maps");
    std::stringstream text;
    text << maps.rdbuf();
    return text.str().find(name) != std::string::npos;
}

/// The test modules, listed.
struct Host {
    ModuleHost host{WPC_TEST_MODULES};
    std::vector<std::string> problems;
    std::vector<DriverModule> installed = host.installed(problems);
};

}  // namespace

TEST_CASE("installed modules are listed; one from another build is refused") {
    Host h;
    REQUIRE(h.installed.size() == 1);
    CHECK(h.installed[0].name == "fake");
    CHECK(h.installed[0].components == std::vector<Component>{Component::Strip});
    REQUIRE(h.problems.size() == 1);
    CHECK(h.problems[0].find("another build") != std::string::npos);
    CHECK_FALSE(mapped("wpc-driver-fake.so"));  // looked at, then unloaded
    CHECK(h.host.location() == WPC_TEST_MODULES);
}

TEST_CASE("a loaded driver works, its errors arrive as wpc::Error, and it unloads") {
    Host h;
    CHECK(h.host.load("fake", DriverSettings("fake", {{"zones", std::vector<double>{2, 5}}, {"typo", true}})) ==
          std::vector<std::string>{"typo"});
    CHECK(mapped("wpc-driver-fake.so"));
    std::vector<Device> devices = h.host.devices("fake");
    REQUIRE(devices.size() == 1);
    REQUIRE(devices[0].zones.size() == 2);
    CHECK(devices[0].zones[1].leds == 5);

    std::vector<Rgb> red(5, Rgb{200, 0, 0});
    std::vector<ZoneFrame> frame = {{1, red}};
    CHECK_THROWS_WITH_AS(h.host.show("fake", 0, frame), "device 0: zone 1 200", Error);

    h.host.unload("fake");
    CHECK_FALSE(mapped("wpc-driver-fake.so"));  // destroying the driver unloaded its module
    CHECK_THROWS_AS(h.host.devices("fake"), Error);
    CHECK_THROWS_AS(h.host.load("missing", DriverSettings("missing", {})), Error);
}

TEST_CASE("a module that fails to create its driver: an error read after the module is gone") {
    // This used to crash: the exception was made by the module's code, the module was unloaded
    // while the exception travelled, and reading it jumped into unmapped memory (SIGSEGV).
    Host h;
    std::string message;
    try {
        h.host.load("fake", DriverSettings("fake", {{"fail_create", true}}));
        FAIL("no error");
    } catch (const Error& e) {
        CHECK_FALSE(mapped("wpc-driver-fake.so"));  // unloaded already, while the error is read
        message = e.what();
    }
    CHECK(message == "the fake driver refused its settings");
}

TEST_CASE("an exception of another type from a module arrives as wpc::Error") {
    Host h;
    h.host.load("fake", DriverSettings("fake", {{"fail_other", true}}));
    CHECK_THROWS_WITH_AS(h.host.devices("fake"), "fake: the fake driver's own error", Error);
}

TEST_CASE("the real driver modules load and unload") {
    ModuleHost host(WPC_BUILT_MODULES);
    std::vector<std::string> problems;
    std::vector<DriverModule> modules = host.installed(problems);
    CHECK(problems.empty());
    std::vector<std::string> names;
    for (const DriverModule& m : modules) names.push_back(m.name);
    CHECK(names == std::vector<std::string>{"fusion2", "openrgb", "razer"});
    // What the Drivers page offers, copied out of the modules.
    REQUIRE(modules[2].options.size() == 1);
    CHECK(modules[2].options[0].key == "direction");
    CHECK(modules[2].options[0].choices == std::vector<std::string>{"across", "down"});
    REQUIRE(modules[0].options.size() == 1);
    CHECK(modules[0].options[0].key == "argb_order");
    CHECK(modules[1].options.empty());
    for (const DriverModule& m : modules) {
        CAPTURE(m.name);
        host.load(m.name, DriverSettings(m.config_table, {}));  // creating opens nothing
        CHECK(mapped("wpc-driver-" + m.name + ".so"));
        host.unload(m.name);
        CHECK_FALSE(mapped("wpc-driver-" + m.name + ".so"));  // no "unique" symbol keeps it loaded
    }

    // A real module refusing its settings (a typo in the config): an error, not a crash.
    CHECK_THROWS_WITH_AS(host.load("fusion2", DriverSettings("motherboard", {{"argb_order", std::string("XYZ")}})),
                         doctest::Contains("argb_order"), Error);
}

TEST_CASE("a file that is not a module is a problem, not a driver") {
    fs::path dir = fs::temp_directory_path() / ("wpc-host-test-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    std::ofstream(dir / "wpc-driver-broken.so") << "not an ELF file";
    ModuleHost host(dir);
    std::vector<std::string> problems;
    CHECK(host.installed(problems).empty());
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("cannot load") != std::string::npos);
    fs::remove_all(dir);
    CHECK(ModuleHost("/nonexistent").location() == "none");
}
