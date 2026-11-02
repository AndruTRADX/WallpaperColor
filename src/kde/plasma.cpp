#include "kde/plasma.hpp"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include <sdbus-c++/sdbus-c++.h>

#include "core/color.hpp"
#include "util/error.hpp"

namespace wpc::kde {

namespace fs = std::filesystem;

namespace {
const char* const SERVICE = "org.kde.plasmashell";
const char* const PATH = "/PlasmaShell";
const char* const INTERFACE = "org.kde.PlasmaShell";
}  // namespace

Plasma::~Plasma() = default;
Plasma::Plasma(Plasma&&) noexcept = default;
Plasma& Plasma::operator=(Plasma&&) noexcept = default;

Plasma Plasma::connect() {
    Plasma plasma;
    try {
        plasma.connection_ = sdbus::createSessionBusConnection();
        plasma.proxy_ = sdbus::createProxy(*plasma.connection_, sdbus::ServiceName{SERVICE},
                                           sdbus::ObjectPath{PATH});
    } catch (const sdbus::Error& e) {
        throw Error(e.what());
    }
    return plasma;
}

Wallpaper Plasma::wallpaper(uint32_t screen) {
    std::map<std::string, sdbus::Variant> settings;
    try {
        proxy_->callMethod("wallpaper").onInterface(INTERFACE).withArguments(screen).storeResultsTo(settings);
    } catch (const sdbus::Error& e) {
        throw Error(e.what());
    }

    // A text setting, or nullopt when it is missing or not text.
    auto text = [&](const std::string& key) -> std::optional<std::string> {
        auto it = settings.find(key);
        if (it == settings.end() || !it->second.containsValueOfType<std::string>()) return std::nullopt;
        return it->second.get<std::string>();
    };

    Wallpaper w;
    w.plugin = text("wallpaperPlugin").value_or("");
    if (std::optional<std::string> image = text("Image")) w.image = url_to_path(*image);
    auto slides = settings.find("SlidePaths");
    if (slides != settings.end() && slides->second.containsValueOfType<std::vector<std::string>>()) {
        for (const std::string& url : slides->second.get<std::vector<std::string>>()) {
            if (std::optional<fs::path> p = url_to_path(url)) w.slide_paths.push_back(*p);
        }
    }
    return w;
}

void Plasma::listen(std::function<void()> callback) {
    try {
        auto on_message = [callback](sdbus::Message) { callback(); };
        connection_->addMatch(
            "type='signal',interface='org.kde.PlasmaShell',member='wallpaperChanged'", on_message);
        connection_->addMatch(
            "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
            "member='NameOwnerChanged',arg0='org.kde.plasmashell'",
            on_message);
        connection_->enterEventLoopAsync();
    } catch (const sdbus::Error& e) {
        throw Error(e.what());
    }
}

// ---------------------------------------------------------------------------------------------

std::optional<fs::path> url_to_path(std::string_view url) {
    if (url.empty() || url == "null") return std::nullopt;
    if (!url.starts_with("file://")) {
        if (url.starts_with('/')) return fs::path(url);
        return std::nullopt;
    }
    std::string_view rest = url.substr(7);
    std::string out;
    for (size_t i = 0; i < rest.size();) {
        if (rest[i] == '%' && i + 2 < rest.size()) {
            if (std::optional<uint8_t> b = parse_hex_byte(rest[i + 1], rest[i + 2])) {
                out += static_cast<char>(*b);
                i += 3;
                continue;
            }
        }
        out += rest[i];
        i += 1;
    }
    return fs::path(out);
}

std::optional<fs::path> resolve_image(const fs::path& path, bool dark) {
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) return path;

    std::vector<std::string> subdirs = dark ? std::vector<std::string>{"images_dark", "images"}
                                            : std::vector<std::string>{"images"};
    for (const std::string& sub : subdirs) {
        fs::directory_iterator it(path / "contents" / sub, ec);
        if (ec) continue;  // no such folder
        std::optional<fs::path> biggest;
        uintmax_t biggest_size = 0;
        for (const fs::directory_entry& entry : it) {
            std::error_code e;
            if (entry.symlink_status(e).type() != fs::file_type::regular) continue;
            uintmax_t size = entry.file_size(e);
            if (e) size = 0;
            // On a tie the last one wins, like Rust's max_by_key.
            if (!biggest || size >= biggest_size) {
                biggest = entry.path();
                biggest_size = size;
            }
        }
        if (biggest) return biggest;
    }
    return std::nullopt;
}

std::optional<fs::path> kdeglobals_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (xdg && fs::path(xdg).is_absolute()) return fs::path(xdg) / "kdeglobals";
    if (home) return fs::path(home) / ".config" / "kdeglobals";
    return std::nullopt;
}

bool dark_mode() {
    std::optional<fs::path> path = kdeglobals_path();
    if (!path) return false;
    std::ifstream file(*path);
    if (!file) return false;
    std::stringstream text;
    text << file.rdbuf();
    return is_dark_scheme(text.str());
}

namespace {

/// Rust's str::trim() for ASCII whitespace.
std::string_view trim(std::string_view s) {
    const char* space = " \t\n\r\f\v";
    size_t start = s.find_first_not_of(space);
    if (start == std::string_view::npos) return {};
    size_t end = s.find_last_not_of(space);
    return s.substr(start, end - start + 1);
}

/// Rust's str::parse::<u32>(): optional '+', then digits, no overflow.
std::optional<uint32_t> parse_u32(std::string_view s) {
    if (s.starts_with('+')) s.remove_prefix(1);
    if (s.empty()) return std::nullopt;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + static_cast<uint64_t>(c - '0');
        if (v > 0xFFFFFFFFu) return std::nullopt;
    }
    return static_cast<uint32_t>(v);
}

}  // namespace

bool is_dark_scheme(std::string_view kdeglobals) {
    bool in_window = false;
    while (!kdeglobals.empty()) {
        size_t end = kdeglobals.find('\n');
        std::string_view line = kdeglobals.substr(0, end);
        kdeglobals = (end == std::string_view::npos) ? std::string_view{} : kdeglobals.substr(end + 1);
        if (line.ends_with('\r')) line.remove_suffix(1);

        if (line.starts_with('[')) {
            in_window = (line == "[Colors:Window]");
            continue;
        }
        const std::string_view key = "BackgroundNormal=";
        if (!in_window || !line.starts_with(key)) continue;

        std::vector<uint32_t> rgb;
        std::string_view values = line.substr(key.size());
        while (true) {
            size_t comma = values.find(',');
            if (std::optional<uint32_t> v = parse_u32(trim(values.substr(0, comma)))) rgb.push_back(*v);
            if (comma == std::string_view::npos) break;
            values.remove_prefix(comma + 1);
        }
        if (rgb.size() >= 3) {
            return (299 * rgb[0] + 587 * rgb[1] + 114 * rgb[2]) / 1000 <= 128;
        }
    }
    return false;
}

}  // namespace wpc::kde
