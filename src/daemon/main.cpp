// wallpapercolord: paints the RGB hardware with the colours of the wallpaper on screen.
//
// This file only picks the real implementation of each port and connects them (the
// "composition root"). What the service does is in service/; the ports' implementations are in
// kde/ (the desktop) and system/ (the rest of the computer).

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include "daemon/kde/plasma.hpp"
#include "daemon/service/events.hpp"
#include "daemon/service/loop.hpp"
#include "daemon/system/clock.hpp"
#include "daemon/system/file_watch.hpp"
#include "daemon/system/logind.hpp"
#include "daemon/system/request_server.hpp"
#include "daemon/system/settings_source.hpp"
#include "drivers/host/driver_host.hpp"
#include "pictures/picture_files.hpp"
#include "protocol/json.hpp"
#include "protocol/socket_path.hpp"
#include "settings/settings_file.hpp"
#include "utils/error.hpp"
#include "utils/event_loop.hpp"
#include "utils/log.hpp"

using namespace wpc;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    try {
        std::optional<fs::path> path = argc > 1 ? std::optional<fs::path>(argv[1]) : default_settings_path();
        if (!path) throw Error("no config path: set HOME or XDG_CONFIG_HOME");

        // First: a second service stops here, before it touches anything of the first one.
        UnixRequestServer requests(ipc::socket_path());

        // The service's one wait: inotify, both D-Buses, the retry and burst timeouts, and the
        // request socket's thread handing a request over.
        EventLoop events;
        InotifyFileWatch files(events);  // one inotify for the settings file and KDE's files
        SettingsFileSource settings(*path);
        ModuleHost drivers(default_driver_folder());
        PictureFiles pictures(default_decoders_module());
        SystemClock clock;
        std::optional<PlasmaDesktop> desktop;
        try {
            desktop.emplace(events);
        } catch (const Error& e) {
            throw Error(std::string("cannot reach the session bus: ") + e.what());
        }
        Logind logind(events);

        Notifications notifications(events);
        Notify notify = [&notifications](Notification n) { notifications.send(std::move(n)); };
        DesktopChangedEvent desktop_changed(*desktop, files, notify);
        SettingsChangedEvent settings_changed(settings, files, notify);
        HardwareChangedEvent hardware_changed(logind, files, notify);
        RequestReceivedEvent request_received(requests, notify, ipc::encode(ipc::Response::error("service busy")));

        ServiceLoop loop(Ports{*desktop, pictures, settings, drivers, clock}, notifications, desktop_changed);
        request_received.start();
        desktop_changed.start();
        try {
            settings_changed.start();
        } catch (const Error& e) {
            log("not watching {}: {}", path->string(), e.what());
        }
        try {
            hardware_changed.start();
        } catch (const Error& e) {
            log("not hearing devices plugged in: {}", e.what());
        }
        loop.run();
    } catch (const Error& e) {
        log("wallpapercolord: {}", e.what());
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        // Not expected (a bug): said, instead of an abort with no message; systemd starts it again.
        log("wallpapercolord: unexpected error: {}", e.what());
        return EXIT_FAILURE;
    }
}
