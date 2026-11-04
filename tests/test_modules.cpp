// Loading and unloading driver modules through the ModuleCatalog: the test modules
// (fake_driver.cpp) and the real ones.

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "drivers/host/services/module_catalog.hpp"
#include "utils/error.hpp"

using namespace wpc;
namespace fs = std::filesystem;

namespace {

/// Whether `file` is mapped into this process now.
bool mapped(const fs::path& file) {
    std::ifstream maps("/proc/self/maps");
    std::stringstream text;
    text << maps.rdbuf();
    return text.str().find(file.string()) != std::string::npos;
}

/// Points the module search at `dir` while it lives.
struct DriverDir {
    explicit DriverDir(const char* dir) { setenv("WPC_DRIVER_DIR", dir, 1); }
    ~DriverDir() { unsetenv("WPC_DRIVER_DIR"); }
};

}  // namespace

TEST_CASE("installed modules are listed; one from another build is refused") {
    DriverDir dir(WPC_TEST_MODULES);
    ModuleCatalog catalog;
    std::vector<std::string> problems;
    std::vector<InstalledModule> modules = catalog.installed(problems);
    REQUIRE(modules.size() == 1);
    CHECK(modules[0].name == "fake");
    CHECK(modules[0].components == std::vector<Component>{Component::Strip});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("another build") != std::string::npos);
    CHECK_FALSE(mapped(modules[0].file));  // looked at, then unloaded
    CHECK(catalog.location() == WPC_TEST_MODULES);
}

TEST_CASE("a loaded driver works, its errors arrive as wpc::Error, and it unloads") {
    DriverDir dir(WPC_TEST_MODULES);
    ModuleCatalog catalog;
    std::vector<std::string> problems;
    InstalledModule fake = catalog.installed(problems).at(0);
    {
        LoadedModule loaded = catalog.create(catalog.load(fake), DriverSettings("fake", {{"zones", std::vector<double>{2, 5}}, {"typo", true}}));
        CHECK(mapped(fake.file));
        CHECK(loaded.unread_settings == std::vector<std::string>{"typo"});
        std::vector<Device> devices = loaded.driver->devices();
        REQUIRE(devices.size() == 1);
        REQUIRE(devices[0].zones.size() == 2);
        CHECK(devices[0].zones[1].leds == 5);

        std::vector<Rgb> red(5, Rgb{200, 0, 0});
        std::vector<ZoneFrame> frame = {{1, red}};
        try {
            loaded.driver->show(0, frame);
            FAIL("no error");
        } catch (const Error& e) {
            CHECK(std::string(e.what()) == "device 0: zone 1 200");
        }
    }
    CHECK_FALSE(mapped(fake.file));  // destroying the driver unloaded its module
}

TEST_CASE("the real driver modules load and unload") {
    DriverDir dir(WPC_BUILT_MODULES);
    ModuleCatalog catalog;
    std::vector<std::string> problems;
    std::vector<InstalledModule> modules = catalog.installed(problems);
    CHECK(problems.empty());
    std::vector<std::string> names;
    for (const InstalledModule& m : modules) names.push_back(m.name);
    CHECK(names == std::vector<std::string>{"fusion2", "openrgb", "razer"});
    // What the Drivers page offers, copied out of the modules.
    REQUIRE(modules[2].options.size() == 1);
    CHECK(modules[2].options[0].key == "direction");
    CHECK(modules[2].options[0].choices == std::vector<std::string>{"across", "down"});
    REQUIRE(modules[0].options.size() == 1);
    CHECK(modules[0].options[0].key == "argb_order");
    CHECK(modules[1].options.empty());
    for (const InstalledModule& m : modules) {
        CAPTURE(m.name);
        {
            LoadedModule loaded = catalog.create(catalog.load(m), DriverSettings(m.config_table, {}));  // creating opens nothing
            CHECK(mapped(m.file));
        }
        CHECK_FALSE(mapped(m.file));  // no "unique" symbol keeps it loaded
    }
}

TEST_CASE("a file that is not a module is an error") {
    CHECK_THROWS_AS(Module::load("/nonexistent/wpc-driver-x.so"), Error);
    CHECK_THROWS_AS(Module::load("/proc/self/exe"), Error);
}
