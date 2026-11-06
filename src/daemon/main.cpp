// wallpapercolord: paints the RGB hardware with the colours of the wallpaper on screen.
//
// This file only picks the real implementations of the service's contracts and connects them
// (the "composition root"). What the service does is in application/.

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include "daemon/application/events/desktop_changed.hpp"
#include "daemon/application/events/hardware_changed.hpp"
#include "daemon/application/events/request_received.hpp"
#include "daemon/application/events/settings_changed.hpp"
#include "daemon/application/features/exchange_messages/exchange_messages.hpp"
#include "daemon/infrastructure/external/driver_modules.hpp"
#include "daemon/infrastructure/external/message_codec.hpp"
#include "daemon/infrastructure/external/pictures.hpp"
#include "daemon/infrastructure/external/settings_source.hpp"
#include "daemon/infrastructure/services/clock.hpp"
#include "daemon/infrastructure/services/colour_scheme.hpp"
#include "daemon/infrastructure/services/file_watch.hpp"
#include "daemon/infrastructure/services/logind.hpp"
#include "daemon/infrastructure/services/memory.hpp"
#include "daemon/infrastructure/services/plasma_shell.hpp"
#include "daemon/infrastructure/services/request_socket.hpp"
#include "daemon/infrastructure/services/wallpaper_packages.hpp"
#include "daemon/presentation/service_loop/service_loop.hpp"
#include "shared/infrastructure/services/settings_file.hpp"
#include "shared/infrastructure/services/socket_path.hpp"
#include "utils/error.hpp"
#include "utils/log.hpp"

using namespace wpc;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    try {
        std::optional<fs::path> path = argc > 1 ? std::optional<fs::path>(argv[1]) : SettingsFile().default_path();
        if (!path) throw Error("no config path: set HOME or XDG_CONFIG_HOME");

        InotifyFileWatch files;  // one inotify thread for the settings file and KDE's files
        SettingsFileSource settings(*path);
        HostDriverModules drivers;
        PictureFilesAdapter pictures;
        ProtocolJsonCodec codec;
        SystemClock clock;
        GlibcMemory memory;
        PlasmaPackages packages;
        KdeColourScheme scheme;
        std::optional<PlasmaShell> plasma;
        try {
            plasma.emplace();
        } catch (const Error& e) {
            throw Error(std::string("cannot reach the session bus: ") + e.what());
        }
        Logind logind;
        UnixRequestSocket socket(SocketPath().path());

        Notifications notifications;
        Notify notify = [&notifications](Notification n) { notifications.send(std::move(n)); };
        DesktopChangedEvent desktop_changed(*plasma, files, notify);
        SettingsChangedEvent settings_changed(settings, files, notify);
        HardwareChangedEvent hardware_changed(logind, files, notify);
        RequestReceivedEvent request_received(socket, notify, write_answer(codec, ipc::Response::error("service busy")));

        ServiceLoop loop(ServiceParts{*plasma, packages, scheme, pictures, settings, drivers, codec, clock, memory},
                         notifications, desktop_changed);
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
    }
}
