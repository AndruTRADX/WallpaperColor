// The service's features and loop, with a fake for every contract: the desktop, the settings,
// the pictures, the drivers, the clock. This is what talking through contracts makes possible:
// no hardware, no files, no D-Bus.

#include <algorithm>

#include <doctest/doctest.h>

#include "daemon/application/events/hardware_changed.hpp"
#include "daemon/application/events/settings_changed.hpp"
#include "daemon/application/features/exchange_messages/exchange_messages.hpp"
#include "daemon/application/features/paint_wallpaper/paint_wallpaper.hpp"
#include "daemon/application/features/report_status/report_status.hpp"
#include "daemon/domain/fade.hpp"
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
    std::function<void(const fs::path&)> opened;  // a file watch would see the picture opened

    Picture read(const fs::path& path, uint32_t) override {
        ++reads;
        if (opened) opened(path);
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

/// Remembers the watches, so a test can see what is watched and make a change happen.
struct FakeFiles final : FileWatch {
    struct Watch {
        int id;
        fs::path folder;
        std::function<void(const FileChange&)> changed;
    };
    std::vector<Watch> watches;
    int trees_scanned = 0;  // watch_tree() calls: the real one reads the folder tree then
    int next_id = 1;

    int watch(const fs::path& folder, uint32_t, std::function<void(const FileChange&)> changed) override {
        watches.push_back(Watch{next_id, folder, std::move(changed)});
        return next_id++;
    }
    std::vector<fs::path> missing;  // watching these fails, as a folder that doesn't exist

    int watch_tree(const fs::path& folder, uint32_t, std::function<void(const FileChange&)> changed) override {
        ++trees_scanned;
        if (std::find(missing.begin(), missing.end(), folder) != missing.end()) throw Error("No such file or directory");
        watches.push_back(Watch{next_id, folder, std::move(changed)});
        return next_id++;
    }
    void unwatch(int id) override { std::erase_if(watches, [&](const Watch& w) { return w.id == id; }); }

    bool watching(const fs::path& folder) const {
        return std::any_of(watches.begin(), watches.end(), [&](const Watch& w) { return w.folder == folder; });
    }
    /// A change in `folder`, as inotify would report it.
    void happen(const FileChange& change) {
        for (const Watch& w : std::vector<Watch>(watches)) {
            if (w.folder == change.folder) w.changed(change);
        }
    }
};

/// The computer's signals, made to happen by the test.
struct FakeSystem final : SystemSignals {
    std::function<void()> woke;
    void listen(std::function<void()> w) override { woke = std::move(w); }
    fs::path device_folder() override { return "/dev"; }
};

/// Lines are the requests' names.
struct FakeCodec final : MessageCodec {
    ipc::Request decode_request(std::string_view line) override {
        if (line == "status") return ipc::Request::Status;
        if (line == "pause") return ipc::Request::Pause;
        if (line == "resume") return ipc::Request::Resume;
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
    EventLoop events;  // nothing waits in it: the fakes send at once
    Notifications notifications{events};
    DesktopChangedEvent desktop_changed{desktop, files, [this](Notification n) { notifications.send(std::move(n)); }};

    /// No settle time: a burst is over as soon as nothing is waiting.
    ServiceLoop loop() {
        return ServiceLoop(ServiceParts{desktop, packages, scheme, pictures, settings, drivers, codec, clock, memory},
                           notifications, desktop_changed, std::chrono::milliseconds(0));
    }

    /// A request from the terminal, as the socket hands it to the loop.
    void request(const std::string& line) {
        notifications.send(RequestNotice{line, std::make_shared<Channel<std::string>>()});
    }

    /// Time passes (only on the service's clock).
    void wait(std::chrono::seconds s) { clock.at += s; }
};

/// R2: a pending retry is never left due; a due one would make the loop spin.
bool retry_overdue(const ServiceLoop& service, FakeClock& clock) {
    return service.state().retry && service.state().retry->at <= clock.now();
}

/// Runs the loop until it has nothing to do, without waiting; how many turns did something.
/// A loop that never runs out of work is a spinning service: it stops at `most` and says so.
int settle(ServiceLoop& service, int most = 20) {
    int turns = 0;
    while (turns < most && service.step(std::chrono::milliseconds(0))) ++turns;
    return turns;
}

WallpaperSettings slideshow(const char* slide) {
    return WallpaperSettings{"org.kde.slideshow", fs::path(slide), {fs::path("/pictures")}};
}

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

// ---------------------------------------------------------------------------------------------
// The loop over time: rules that keep the service from going deaf (see docs/ARCHITECTURE.md).
// Each test drives ServiceLoop::step() with a fake clock, the way the events would.

TEST_CASE("R1: while the desktop has no screen, the slideshow stays followed and it is asked again") {
    Setup s;
    s.desktop.on = slideshow("/pictures/red.png");
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    REQUIRE(s.drivers.shown.size() == 1);
    REQUIRE(s.files.watching("/pictures"));

    // The monitor goes off: Plasma has no screen and answers with no plugin.
    s.desktop.on = WallpaperSettings{};
    s.notifications.send(DesktopChangedNotice{});
    CHECK(settle(service) == 1);
    CHECK(s.files.watching("/pictures"));
    REQUIRE(service.state().retry);
    CHECK(report_status(service.state()).image == fs::path("/pictures/red.png"));

    // Still off a while later: asked again, nothing painted, never overdue.
    s.wait(std::chrono::seconds(2));
    CHECK(settle(service) == 1);
    CHECK_FALSE(retry_overdue(service, s.clock));
    CHECK(s.drivers.shown.size() == 1);

    // Back, on another slide, and no signal says so: the retry finds it.
    s.desktop.on = slideshow("/pictures/blue.png");
    s.wait(std::chrono::seconds(60));
    CHECK(settle(service) == 1);
    CHECK(s.drivers.shown.size() == 2);
    CHECK_FALSE(service.state().retry);
    CHECK_FALSE(report_status(service.state()).error);
    CHECK(report_status(service.state()).image == fs::path("/pictures/blue.png"));
}

TEST_CASE("R1: a slide opened by the desktop is painted without any other signal") {
    Setup s;
    s.desktop.on = slideshow("/pictures/red.png");
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);

    s.desktop.on = slideshow("/pictures/blue.png");
    s.files.happen(FileChange{.folder = "/pictures", .name = "blue.png", .opened = true});
    CHECK(settle(service) == 1);
    CHECK(report_status(service.state()).image == fs::path("/pictures/blue.png"));
    CHECK(s.drivers.shown.size() == 2);
}

TEST_CASE("R2: paused with a retry pending, the loop goes quiet") {
    Setup s;
    s.drivers.unplugged = true;
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    REQUIRE(service.state().retry);

    s.request("pause");
    CHECK(settle(service) == 1);
    REQUIRE(service.state().paused);

    s.wait(std::chrono::seconds(10));
    CHECK(settle(service) <= 1);  // at most the retry, once
    CHECK_FALSE(retry_overdue(service, s.clock));

    // Resumed: painted at once.
    s.drivers.unplugged = false;
    size_t shown = s.drivers.shown.size();
    s.request("resume");
    CHECK(settle(service) == 1);
    CHECK(s.drivers.shown.size() > shown);
    CHECK_FALSE(report_status(service.state()).error);
}

TEST_CASE("R2: a retry that fails again is moved on, further each time") {
    Setup s;
    s.drivers.unplugged = true;
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    REQUIRE(service.state().retry);
    RetryDelay first = service.state().retry->delay;

    s.wait(std::chrono::seconds(2));
    CHECK(settle(service) == 1);
    CHECK_FALSE(retry_overdue(service, s.clock));
    REQUIRE(service.state().retry);
    CHECK(service.state().retry->delay > first);
}

TEST_CASE("R3: a folder created inside the slideshow is watched too") {
    Setup s;
    s.desktop.on = slideshow("/pictures/red.png");
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    REQUIRE(s.files.trees_scanned == 1);

    s.files.happen(FileChange{.folder = "/pictures", .name = "new", .created = true, .is_folder = true});
    settle(service);
    CHECK(s.files.trees_scanned == 2);  // the tree read again, the new folder in it
    CHECK(s.files.watching("/pictures"));
}

TEST_CASE("R3: the slideshow folder removed is watched again once it is back") {
    Setup s;
    s.desktop.on = slideshow("/pictures/red.png");
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);

    // Removed (or renamed away): its watch ends, and it cannot be watched again yet.
    s.files.missing = {"/pictures"};
    s.files.happen(FileChange{.folder = "/pictures", .name = "", .removed = true, .is_folder = true});
    CHECK(settle(service) == 1);
    REQUIRE(service.state().retry);
    CHECK(report_status(service.state()).error == "not watching /pictures: No such file or directory");

    s.wait(std::chrono::seconds(2));
    CHECK(settle(service) == 1);  // still missing: tried again, later
    CHECK_FALSE(retry_overdue(service, s.clock));

    // Back: watched again, and the retry is over.
    s.files.missing.clear();
    s.wait(std::chrono::seconds(60));
    CHECK(settle(service) == 1);
    CHECK(s.files.watching("/pictures"));
    CHECK_FALSE(service.state().retry);
    CHECK_FALSE(report_status(service.state()).error);
}

TEST_CASE("R3: watching the folders again keeps no old watch, and misses nothing in between") {
    Setup s;
    s.desktop.on = slideshow("/pictures/red.png");
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    s.files.happen(FileChange{.folder = "/pictures", .name = "a", .created = true, .is_folder = true});
    s.files.happen(FileChange{.folder = "/pictures", .name = "b", .created = true, .is_folder = true});
    settle(service);
    CHECK(s.files.trees_scanned == 2);  // one burst, read once
    CHECK(s.files.watches.size() == 1);
}

TEST_CASE("R5: changes lost by the kernel read the folders again and check the wallpaper") {
    Setup s;
    s.desktop.on = slideshow("/pictures/red.png");
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);

    s.desktop.on = slideshow("/pictures/blue.png");
    s.files.happen(FileChange{.folder = "/pictures", .name = "", .is_folder = true, .overflow = true});
    CHECK(settle(service) == 1);
    CHECK(s.files.trees_scanned == 2);
    CHECK(report_status(service.state()).image == fs::path("/pictures/blue.png"));
}

TEST_CASE("R5: changes lost by the kernel reload the settings") {
    Setup s;
    SettingsChangedEvent settings_changed(s.settings, s.files, [&](Notification n) { s.notifications.send(std::move(n)); });
    settings_changed.start();
    ServiceLoop service = s.loop();
    s.settings.config.led.mode = Mode::Single;
    s.files.happen(FileChange{.folder = "/fake", .name = "", .is_folder = true, .overflow = true});
    CHECK(settle(service) == 1);
    CHECK(service.state().config.led.mode == Mode::Single);
}

TEST_CASE("the service reading the new slide is not taken for another change") {
    Setup s;
    s.desktop.on = slideshow("/pictures/red.png");
    s.pictures.opened = [&](const fs::path& p) {
        s.files.happen(FileChange{.folder = p.parent_path(), .name = p.filename().string(), .opened = true});
    };
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    CHECK_FALSE(service.step(std::chrono::milliseconds(0)));

    s.desktop.on = slideshow("/pictures/blue.png");
    s.files.happen(FileChange{.folder = "/pictures", .name = "blue.png", .opened = true});  // Plasma opens it
    CHECK(settle(service) == 1);  // and the service's own read of it wakes nothing
    CHECK(s.pictures.reads == 2);
}

TEST_CASE("R4: a device plugged in is painted again without a new wallpaper") {
    Setup s;
    FakeSystem system;
    HardwareChangedEvent hardware(system, s.files, [&](Notification n) { s.notifications.send(std::move(n)); });
    hardware.start();
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    REQUIRE(s.drivers.shown.size() == 1);

    // A keyboard plugged in again: the kernel adds a few files at once.
    for (const char* name : {"hidraw7", "hidraw8", "hidraw9"}) {
        s.files.happen(FileChange{.folder = "/dev", .name = name, .created = true});
    }
    CHECK(settle(service) == 1);       // one burst, one repaint
    CHECK(s.drivers.shown.size() == 2);
    CHECK(s.pictures.reads == 1);      // the same colours, not read again
}

TEST_CASE("R4: waking from sleep paints again") {
    Setup s;
    FakeSystem system;
    HardwareChangedEvent hardware(system, s.files, [&](Notification n) { s.notifications.send(std::move(n)); });
    hardware.start();
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    REQUIRE(system.woke);

    system.woke();
    CHECK(settle(service) == 1);
    CHECK(s.drivers.shown.size() == 2);
    CHECK_FALSE(service.state().retry);
}

TEST_CASE("R4: paused, the hardware coming back paints nothing and the loop stays quiet") {
    Setup s;
    FakeSystem system;
    HardwareChangedEvent hardware(system, s.files, [&](Notification n) { s.notifications.send(std::move(n)); });
    hardware.start();
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    s.request("pause");
    settle(service);

    system.woke();
    CHECK(settle(service) == 1);
    CHECK(s.drivers.shown.size() == 1);
    CHECK_FALSE(service.state().retry);
}

TEST_CASE("the loop does nothing, and does not wait, when nothing happened") {
    Setup s;
    ServiceLoop service = s.loop();
    service.check_wallpaper(false);
    CHECK_FALSE(service.step(std::chrono::milliseconds(0)));
    s.request("status");
    CHECK(service.step(std::chrono::milliseconds(0)));  // answered
    CHECK(s.drivers.shown.size() == 1);                   // and nothing painted for it
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

namespace {

/// Two devices, a fan and a RAM stick; remembers every frame each one got.
struct TwoDevices final : DriverModules {
    std::map<size_t, std::vector<std::vector<Rgb>>> shown;  // device → its frames
    std::vector<DriverModule> installed(std::vector<std::string>&) override { return {}; }
    std::vector<std::string> load(const std::string&, const DriverSettings&) override { return {}; }
    void unload(const std::string&) override {}
    std::vector<Device> devices(const std::string&) override {
        return {Device{"fan", "Fan", {Zone{"Ring", 8, Component::Fans, Shape::Ring}}, {}},
                Device{"ram", "RAM",
                       {Zone{"Top", 3, Component::Ram, Shape::Line}, Zone{"Bottom", 2, Component::Ram, Shape::Line}},
                       {}}};
    }
    void show(const std::string&, size_t device, std::span<const ZoneFrame> zones) override {
        std::vector<Rgb> frame;
        for (const ZoneFrame& z : zones) frame.insert(frame.end(), z.colours.begin(), z.colours.end());
        shown[device].push_back(std::move(frame));
    }
    std::string location() override { return "/fake/drivers"; }
};

}  // namespace

TEST_CASE("a fade sends a device its first frame, then only the frames that change it") {
    TwoDevices drivers;
    FakeClock clock;
    ServiceState state;
    state.drivers.loaded["two"] = DriverSettings();
    state.config.led.fade = 0.6;
    std::vector<Rgb> stops = {Rgb{255, 0, 0}, Rgb{0, 0, 255}, Rgb{0, 255, 0}};
    paint_colours(state, drivers, clock, stops, Mode::Arc);
    REQUIRE(drivers.shown[0].size() == 1);  // nothing shown before: no fade
    std::vector<Rgb> fan = drivers.shown[0].back();
    std::vector<Rgb> ram = drivers.shown[1].back();

    // Only the RAM changes: it fades, frame by frame; the fan is sent its colours once, again.
    state.config.calibration.entries.push_back(Calibration{"RAM", std::nullopt, {0.5, 1.0, 1.0}});
    drivers.shown.clear();
    TimePoint start = clock.now();
    paint_colours(state, drivers, clock, stops, Mode::Arc);
    CHECK(drivers.shown[0] == std::vector<std::vector<Rgb>>{fan});
    REQUIRE(drivers.shown[1].size() > 1);
    CHECK(drivers.shown[1].size() <= static_cast<size_t>(fade_frames(0.6, true)));
    for (size_t f = 1; f < drivers.shown[1].size(); ++f) CHECK(drivers.shown[1][f] != drivers.shown[1][f - 1]);
    CHECK(drivers.shown[1].back() != ram);
    CHECK(drivers.shown[1].back() == state.drivers.shown.at("two ram"));  // it ends on the new colours
    CHECK(clock.now() - start == (fade_frames(0.6, true) - 1) * std::chrono::milliseconds(1000 / FADE_FPS));
}
