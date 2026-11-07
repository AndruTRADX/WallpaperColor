#include "daemon/infrastructure/services/plasma_shell.hpp"

#include <cstdlib>
#include <map>

#include <sdbus-c++/sdbus-c++.h>

#include "utils/error.hpp"
#include "utils/event_loop.hpp"
#include "utils/url.hpp"

namespace wpc {

namespace fs = std::filesystem;

namespace {
const char* const SERVICE = "org.kde.plasmashell";
const char* const PATH = "/PlasmaShell";
const char* const INTERFACE = "org.kde.PlasmaShell";
}  // namespace

PlasmaShell::PlasmaShell(EventLoop& loop) : loop_(loop) {
    try {
        connection_ = sdbus::createSessionBusConnection();
        proxy_ = sdbus::createProxy(*connection_, sdbus::ServiceName{SERVICE}, sdbus::ObjectPath{PATH});
    } catch (const sdbus::Error& e) {
        throw Error(e.what());
    }
}

PlasmaShell::~PlasmaShell() {
    if (bus_fds_) remove_bus(loop_, *bus_fds_);
}

WallpaperSettings PlasmaShell::current() {
    std::map<std::string, sdbus::Variant> settings;
    try {
        proxy_->callMethod("wallpaper").onInterface(INTERFACE).withArguments(uint32_t{0}).storeResultsTo(settings);
    } catch (const sdbus::Error& e) {
        throw Error(e.what());
    }

    // A text setting, or nullopt when it is missing or not text.
    auto text = [&](const std::string& key) -> std::optional<std::string> {
        auto it = settings.find(key);
        if (it == settings.end() || !it->second.containsValueOfType<std::string>()) return std::nullopt;
        return it->second.get<std::string>();
    };

    WallpaperSettings w;
    w.plugin = text("wallpaperPlugin").value_or("");
    if (std::optional<std::string> image = text("Image")) w.image = url_to_path(*image);
    auto slides = settings.find("SlidePaths");
    if (w.plugin == "org.kde.slideshow" && slides != settings.end() &&
        slides->second.containsValueOfType<std::vector<std::string>>()) {
        for (const std::string& url : slides->second.get<std::vector<std::string>>()) {
            if (std::optional<fs::path> p = url_to_path(url)) w.slide_folders.push_back(*p);
        }
    }
    return w;
}

void PlasmaShell::listen(std::function<void()> changed) {
    try {
        auto on_message = [changed](sdbus::Message) { changed(); };
        connection_->addMatch("type='signal',interface='org.kde.PlasmaShell',member='wallpaperChanged'", on_message);
        connection_->addMatch(
            "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
            "member='NameOwnerChanged',arg0='org.kde.plasmashell'",
            on_message);
        bus_fds_ = add_bus(loop_, *connection_);
    } catch (const sdbus::Error& e) {
        throw Error(e.what());
    }
}

std::vector<fs::path> PlasmaShell::settings_files() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    fs::path config;
    if (xdg && fs::path(xdg).is_absolute()) config = xdg;
    else if (home) config = fs::path(home) / ".config";
    else return {};
    return {config / "plasma-org.kde.plasma.desktop-appletsrc", config / "kdeglobals"};
}

}  // namespace wpc
