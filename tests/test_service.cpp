// The service's features and loop, with a fake for every contract: the desktop, the settings,
// the pictures, the drivers, the clock. This is what talking through contracts makes possible:
// no hardware, no files, no D-Bus.

#include <doctest/doctest.h>

#include "daemon/application/features/exchange_messages/exchange_messages.hpp"
#include "daemon/application/features/report_status/report_status.hpp"
#include "daemon/presentation/requests/requests.hpp"
#include "daemon/presentation/service_loop/service_loop.hpp"
#include "utils/error.hpp"

using namespace wpc;
namespace fs = std::filesystem;

namespace {

struct FakeDesktop final : DesktopWallpaper, DesktopSignals {
    WallpaperSettings on{"org.kde.image", fs::path("/pictures/red.png"), {}};
    WallpaperSettings current() override { return on; }
    void listen(std::function<void()>) override {}
    std::vector<fs::path> settings_files() override { return {}; }
};

struct FakePackages final : WallpaperPackages {
    std::optional<fs::path> picture(const fs::path& image, bool) override { return image; }
};

struct FakeScheme final : ColourScheme {
    bool is_dark() override { return false; }
};

struct FakeSettings final : SettingsSource {
    Config config = [] {
        Config c;
        c.led.fade = 0;  // one frame per paint
        return c;
    }();
    Config load() override { return config; }
    fs::path location() override { return "/fake/config.toml"; }
};

/// Every picture is plain red.
struct FakePictures final : Pictures {
    int reads = 0;
    Picture read(const fs::path&, uint32_t) override {
        ++reads;
        Picture p{8, 8, {}, {}};
        for (int i = 0; i < 64; ++i) p.rgb8.insert(p.rgb8.end(), {220, 20, 20});
        return p;
    }
    std::optional<int64_t> modified_time(const fs::path&) override { return 1; }
};

/// One driver, "cooler", with one fan; remembers what it showed, fails when told to.
struct FakeDrivers final : DriverModules {
    std::vector<std::string> in_memory;
    std::vector<std::vector<Rgb>> shown;
    bool unplugged = false;

    std::vector<DriverModule> installed(std::vector<std::string>&) override {
        return {DriverModule{"cooler", "cooler", {Component::Fans}}};
    }
    std::vector<std::string> load(const std::string& name, const DriverSettings&) override {
        in_memory.push_back(name);
        return {};
    }
    void unload(const std::string& name) override { std::erase(in_memory, name); }
    std::vector<Device> devices(const std::string&) override {
        return {Device{"aio", "AIO", {Zone{"Fan", 8, Component::Fans, Shape::Ring}}, {}}};
    }
    void show(const std::string&, size_t, std::span<const ZoneFrame> zones) override {
        shown.emplace_back(zones[0].colours.begin(), zones[0].colours.end());
        if (unplugged) throw Error("unplugged");
    }
    std::string location() override { return "/fake/drivers"; }
};

struct FakeClock final : Clock {
    TimePoint at{};
    TimePoint now() override { return at; }
    void wait_until(TimePoint time) override { at = std::max(at, time); }
    uint64_t unix_seconds() override { return 1000; }
};

struct FakeMemory final : Memory {
    int releases = 0;
    void release() override { ++releases; }
};

struct FakeFiles final : FileWatch {
    int watch(const fs::path&, uint32_t, std::function<void(const FileChange&)>) override { return 1; }
    int watch_tree(const fs::path&, uint32_t, std::function<void(const FileChange&)>) override { return 1; }
    void unwatch(int) override {}
};

/// Lines are the requests' names.
struct FakeCodec final : MessageCodec {
    ipc::Request decode_request(std::string_view line) override {
        if (line == "status") return ipc::Request::Status;
        if (line == "pause") return ipc::Request::Pause;
        throw Error("unknown command");
    }
    std::string encode(const ipc::Response& r) override {
        return r.kind == ipc::Response::Kind::Error ? "error: " + r.message : "ok";
    }
};

struct Setup {
    FakeDesktop desktop;
    FakePackages packages;
    FakeScheme scheme;
    FakeSettings settings;
    FakePictures pictures;
    FakeDrivers drivers;
    FakeCodec codec;
    FakeClock clock;
    FakeMemory memory;
    FakeFiles files;
    Notifications notifications;
    DesktopChangedEvent desktop_changed{desktop, files, [](Notification) {}};

    ServiceLoop loop() {
        return ServiceLoop(ServiceParts{desktop, packages, scheme, pictures, settings, drivers, codec, clock, memory},
                           notifications, desktop_changed);
    }
};

}  // namespace

TEST_CASE("the service paints the picture's colours") {
    Setup s;
    ServiceLoop service = s.loop();
    CHECK(s.drivers.in_memory == std::vector<std::string>{"cooler"});  // the settings were applied at start
    service.check_wallpaper(true);

    REQUIRE(s.drivers.shown.size() == 1);
    CHECK(s.drivers.shown[0][0].r > 200);  // the picture's red
    ipc::Status status = report_status(service.state());
    CHECK_FALSE(status.error);
    REQUIRE(status.devices.size() == 1);
    CHECK(status.devices[0].name == "AIO");
    CHECK(status.stops.size() == 3);
    CHECK(status.image == fs::path("/pictures/red.png"));
    CHECK(status.last_sent == 1000);
    CHECK(s.memory.releases == 1);  // after reading the picture
}

TEST_CASE("a problem painting is shown and tried again, without reading the picture again") {
    Setup s;
    s.drivers.unplugged = true;
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    ipc::Status status = report_status(service.state());
    REQUIRE(status.error);
    CHECK(*status.error == "cooler: unplugged");
    CHECK(status.drivers.at(0).error == "unplugged");
    CHECK(status.devices.size() == 1);  // what answered is still known
    REQUIRE(service.state().retry);

    s.drivers.unplugged = false;
    service.check_wallpaper(false);  // the retry: same picture, painted again
    CHECK(s.drivers.shown.size() == 2);
    CHECK_FALSE(report_status(service.state()).error);
    CHECK_FALSE(report_status(service.state()).drivers.at(0).error);
    CHECK(s.pictures.reads == 1);
}

TEST_CASE("while the desktop has no screen, the slideshow stays followed and it is asked again") {
    Setup s;
    s.desktop.on = WallpaperSettings{"org.kde.slideshow", fs::path("/pictures/red.png"), {fs::path("/pictures")}};
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    REQUIRE(s.drivers.shown.size() == 1);

    s.desktop.on = WallpaperSettings{};  // the monitor went off: Plasma answers with no plugin
    service.check_wallpaper(false);
    CHECK(service.state().slide_folders == std::vector<fs::path>{"/pictures"});
    REQUIRE(service.state().retry);
    CHECK(report_status(service.state()).image == fs::path("/pictures/red.png"));

    s.desktop.on = WallpaperSettings{"org.kde.slideshow", fs::path("/pictures/blue.png"), {fs::path("/pictures")}};
    service.check_wallpaper(false);  // the retry: the screen is back with another slide
    CHECK(s.drivers.shown.size() == 2);
    CHECK_FALSE(service.state().retry);
    CHECK_FALSE(report_status(service.state()).error);
    CHECK(report_status(service.state()).image == fs::path("/pictures/blue.png"));
}

TEST_CASE("a driver is loaded only while one of its components is chosen") {
    Setup s;
    ServiceLoop service = s.loop();
    s.settings.config.components.on[static_cast<size_t>(Component::Fans)] = false;
    s.settings.config.components.chosen = true;
    service.settings_changed();
    CHECK(s.drivers.in_memory.empty());
    CHECK_FALSE(report_status(service.state()).drivers.at(0).loaded);
    CHECK(s.memory.releases == 1);  // after unloading it

    s.settings.config.components.on[static_cast<size_t>(Component::Fans)] = true;
    service.settings_changed();
    CHECK(s.drivers.in_memory == std::vector<std::string>{"cooler"});
}

TEST_CASE("the same picture is not read twice") {
    Setup s;
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    service.check_wallpaper(false);  // nothing changed: nothing to do
    service.check_wallpaper(true);   // painted again, from the colours already taken
    CHECK(s.pictures.reads == 1);
    CHECK(s.drivers.shown.size() == 2);
}

TEST_CASE("requests run their feature") {
    Setup s;
    ServiceLoop service = s.loop();
    ServiceState state = service.state();
    bool paint_now = false;
    CHECK(run_request(ipc::Request::Pause, state, paint_now) == ipc::Response::ok());
    CHECK(state.paused);
    CHECK_FALSE(paint_now);
    run_request(ipc::Request::Test, state, paint_now);
    CHECK(state.testing);
    CHECK(paint_now);  // white at once

    ReadRequest bad = read_request(s.codec, "dance");
    CHECK_FALSE(bad.request);
    CHECK(bad.problem == "bad request: unknown command");
    CHECK(read_request(s.codec, "status").request == ipc::Request::Status);
}
