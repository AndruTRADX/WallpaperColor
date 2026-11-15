#include "daemon/kde/plasma.hpp"

#include <map>

#include <sdbus-c++/sdbus-c++.h>

#include "daemon/kde/wallpaper.hpp"
#include "utils/error.hpp"
#include "utils/files.hpp"
#include "utils/url.hpp"

namespace wpc {

namespace fs = std::filesystem;

namespace {
const char* const SERVICE = "org.kde.plasmashell";
const char* const PATH = "/PlasmaShell";
const char* const INTERFACE = "org.kde.PlasmaShell";
}  // namespace

PlasmaDesktop::PlasmaDesktop(EventLoop& loop) : loop_(loop) {
    try {
        connection_ = sdbus::createSessionBusConnection();
        proxy_ = sdbus::createProxy(*connection_, sdbus::ServiceName{SERVICE}, sdbus::ObjectPath{PATH});
    } catch (const sdbus::Error& e) {
        throw Error(e.what());
    }
}

PlasmaDesktop::~PlasmaDesktop() {
    if (bus_fds_) remove_bus(loop_, *bus_fds_);
}

OnScreen PlasmaDesktop::on_screen() {
    std::map<std::string, sdbus::Variant> settings;
    try {
        proxy_->callMethod("wallpaper")
            .onInterface(INTERFACE)
            .withTimeout(CALL_TIMEOUT)
            .withArguments(uint32_t{0})
            .storeResultsTo(settings);
    } catch (const sdbus::Error& e) {
        throw Error(e.what());
    }

    // A text setting, or nullopt when it is missing or not text.
    auto text = [&](const std::string& key) -> std::optional<std::string> {
        auto it = settings.find(key);
        if (it == settings.end() || !it->second.containsValueOfType<std::string>()) return std::nullopt;
        return it->second.get<std::string>();
    };

    OnScreen on;
    on.plugin = text("wallpaperPlugin").value_or("");
    auto slides = settings.find("SlidePaths");
    if (on.plugin == "org.kde.slideshow" && slides != settings.end() &&
        slides->second.containsValueOfType<std::vector<std::string>>()) {
        for (const std::string& url : slides->second.get<std::vector<std::string>>()) {
            if (std::optional<fs::path> p = url_to_path(url)) on.slide_folders.push_back(*p);
        }
    }
    std::optional<fs::path> image;
    if (std::optional<std::string> url = text("Image")) image = url_to_path(*url);
    if (image) on.picture = kde::package_picture(*image, kde::colour_scheme_is_dark());
    return on;
}

void PlasmaDesktop::listen(std::function<void()> changed) {
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

std::vector<fs::path> PlasmaDesktop::settings_files() {
    std::optional<fs::path> config = config_home();
    if (!config) return {};
    return {*config / "plasma-org.kde.plasma.desktop-appletsrc", *config / "kdeglobals"};
}

}  // namespace wpc
