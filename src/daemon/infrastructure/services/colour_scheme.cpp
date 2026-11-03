#include "daemon/infrastructure/services/colour_scheme.hpp"

#include <charconv>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

namespace wpc {

namespace fs = std::filesystem;

bool KdeColourScheme::is_dark() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    fs::path path;
    if (xdg && fs::path(xdg).is_absolute()) path = fs::path(xdg) / "kdeglobals";
    else if (home) path = fs::path(home) / ".config" / "kdeglobals";
    else return false;
    std::ifstream file(path);
    if (!file) return false;
    std::stringstream text;
    text << file.rdbuf();
    return is_dark_text(text.str());
}

namespace {

/// `s` without spaces, tabs and line breaks at both ends.
std::string_view trim(std::string_view s) {
    const char* space = " \t\n\r\f\v";
    size_t start = s.find_first_not_of(space);
    if (start == std::string_view::npos) return {};
    size_t end = s.find_last_not_of(space);
    return s.substr(start, end - start + 1);
}

/// The whole of `s` as a number, or nullopt.
std::optional<uint32_t> parse_u32(std::string_view s) {
    uint32_t v = 0;
    auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size()) return std::nullopt;
    return v;
}

}  // namespace

bool KdeColourScheme::is_dark_text(std::string_view kdeglobals) {
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

}  // namespace wpc
