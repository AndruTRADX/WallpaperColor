#include "daemon/kde/wallpaper.hpp"

#include <charconv>
#include <string>
#include <vector>

#include "utils/error.hpp"
#include "utils/files.hpp"

namespace wpc::kde {

namespace fs = std::filesystem;

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

std::optional<fs::path> package_picture(const fs::path& image, bool dark) {
    std::error_code ec;
    if (fs::is_regular_file(image, ec)) return image;

    std::vector<std::string> subdirs = dark ? std::vector<std::string>{"images_dark", "images"}
                                            : std::vector<std::string>{"images"};
    for (const std::string& sub : subdirs) {
        fs::directory_iterator it(image / "contents" / sub, ec);
        if (ec) continue;  // no such folder
        std::optional<fs::path> biggest;
        uintmax_t biggest_size = 0;
        for (const fs::directory_entry& entry : it) {
            std::error_code e;
            if (entry.symlink_status(e).type() != fs::file_type::regular) continue;
            uintmax_t size = entry.file_size(e);
            if (e) size = 0;
            // On a tie the last one wins (`>=`).
            if (!biggest || size >= biggest_size) {
                biggest = entry.path();
                biggest_size = size;
            }
        }
        if (biggest) return biggest;
    }
    return std::nullopt;
}

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
            return (299ull * rgb[0] + 587ull * rgb[1] + 114ull * rgb[2]) / 1000 <= 128;
        }
    }
    return false;
}

bool colour_scheme_is_dark() {
    std::optional<fs::path> config = config_home();
    if (!config) return false;
    try {
        std::optional<std::string> text = read_file(*config / "kdeglobals");
        return text && is_dark_scheme(*text);
    } catch (const Error&) {
        return false;  // unreadable: a light scheme, the default
    }
}

}  // namespace wpc::kde
