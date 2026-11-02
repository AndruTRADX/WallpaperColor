#include "core/config.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>

#include <toml++/toml.hpp>

#include "util/error.hpp"

namespace wpc {

std::string_view mode_name(Mode mode) {
    return mode == Mode::Arc ? "arc" : "single";
}

std::string_view mode_label(Mode mode) {
    return mode == Mode::Arc ? "Arc" : "Single";
}

// ---------------------------------------------------------------------------------------------
// Matching calibrations

static std::string lowercase(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

/// Case-insensitive "hay contains needle".
static bool contains(std::string_view hay, std::string_view needle) {
    return lowercase(hay).find(lowercase(needle)) != std::string::npos;
}

std::array<float, 3> Config::gain(std::string_view device, std::string_view zone) const {
    for (const Calibration& c : calibration) {
        bool zone_matches = !c.zone || contains(zone, *c.zone);
        if (contains(device, c.device) && zone_matches) {
            std::array<float, 3> out;
            for (int i = 0; i < 3; ++i) out[i] = static_cast<float>(std::clamp(c.gain[i], 0.0, 1.0));
            return out;
        }
    }
    return {1.0f, 1.0f, 1.0f};
}

// ---------------------------------------------------------------------------------------------
// Paths and loading

std::optional<std::filesystem::path> Config::default_path() {
    std::filesystem::path base;
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (xdg && std::filesystem::path(xdg).is_absolute()) {
        base = xdg;
    } else if (home) {
        base = std::filesystem::path(home) / ".config";
    } else {
        return std::nullopt;
    }
    return base / "wallpapercolor" / "config.toml";
}

Config Config::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (errno == ENOENT) return Config{};
        throw Error(std::format("{}: {}", path.string(), os_error(errno)));
    }
    std::stringstream text;
    text << file.rdbuf();
    try {
        return from_toml(text.str());
    } catch (const Error& e) {
        throw Error(std::format("{}: {}", path.string(), e.what()));
    }
}

// ---------------------------------------------------------------------------------------------
// Parsing. toml++ reads the file; the checks below add what the Rust version got from serde:
// unknown keys and wrong types are errors, with messages worded the same way.

namespace {

/// An error about `node`, with its position in the file.
[[noreturn]] void fail_at(const toml::source_region& where, const std::string& message) {
    throw Error(std::format("TOML parse error at line {}, column {}: {}", where.begin.line,
                            where.begin.column, message));
}

/// "expected one of `a`, `b`, `c`" like serde words it.
std::string expected_list(const std::vector<std::string_view>& names) {
    auto quoted = [](std::string_view n) { return std::format("`{}`", n); };
    if (names.empty()) return "there are no fields";
    if (names.size() == 1) return "expected " + quoted(names[0]);
    if (names.size() == 2) return std::format("expected {} or {}", quoted(names[0]), quoted(names[1]));
    std::string out = "expected one of ";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += ", ";
        out += quoted(names[i]);
    }
    return out;
}

/// Every key of `table` must be one of `allowed`.
void check_keys(const toml::table& table, const std::vector<std::string_view>& allowed) {
    for (auto&& [key, node] : table) {
        if (std::find(allowed.begin(), allowed.end(), key.str()) == allowed.end()) {
            fail_at(key.source(), std::format("unknown field `{}`, {}", key.str(), expected_list(allowed)));
        }
    }
}

const toml::table& as_table(const toml::node& node) {
    if (!node.is_table()) fail_at(node.source(), "invalid type: expected a table");
    return *node.as_table();
}

std::string read_string(const toml::node& node) {
    if (!node.is_string()) fail_at(node.source(), "invalid type: expected a string");
    return node.as_string()->get();
}

/// A float setting; integers are accepted too (`gain = [1, 0.9, 0.9]`).
double read_float(const toml::node& node) {
    if (node.is_floating_point()) return node.as_floating_point()->get();
    if (node.is_integer()) return static_cast<double>(node.as_integer()->get());
    fail_at(node.source(), "invalid type: expected f64");
}

/// A whole number setting between 0 and `max`.
uint64_t read_unsigned(const toml::node& node, uint64_t max, std::string_view type_name) {
    if (!node.is_integer()) fail_at(node.source(), std::format("invalid type: expected {}", type_name));
    int64_t v = node.as_integer()->get();
    if (v < 0 || static_cast<uint64_t>(v) > max) {
        fail_at(node.source(), std::format("invalid value: integer `{}`, expected {}", v, type_name));
    }
    return static_cast<uint64_t>(v);
}

Mode read_mode(const toml::node& node) {
    std::string name = read_string(node);
    if (name == "arc") return Mode::Arc;
    if (name == "single") return Mode::Single;
    fail_at(node.source(), std::format("unknown variant `{}`, expected `arc` or `single`", name));
}

void read_led(const toml::table& t, LedConfig& led) {
    check_keys(t, {"mode"});
    if (const toml::node* n = t.get("mode")) led.mode = read_mode(*n);
}

void read_extract(const toml::table& t, ExtractConfig& e) {
    check_keys(t, {"palette_size", "sample_size", "min_saturation", "min_chroma", "min_share",
                   "skin_weight", "pastel_gain"});
    if (const toml::node* n = t.get("palette_size")) {
        e.palette_size = read_unsigned(*n, std::numeric_limits<size_t>::max(), "usize");
    }
    if (const toml::node* n = t.get("sample_size")) {
        e.sample_size = static_cast<uint32_t>(read_unsigned(*n, std::numeric_limits<uint32_t>::max(), "u32"));
    }
    if (const toml::node* n = t.get("min_saturation")) e.min_saturation = read_float(*n);
    if (const toml::node* n = t.get("min_chroma")) e.min_chroma = read_float(*n);
    if (const toml::node* n = t.get("min_share")) e.min_share = read_float(*n);
    if (const toml::node* n = t.get("skin_weight")) e.skin_weight = read_float(*n);
    if (const toml::node* n = t.get("pastel_gain")) e.pastel_gain = read_float(*n);
}

void read_openrgb(const toml::table& t, OpenRgbConfig& o) {
    check_keys(t, {"address", "client_name"});
    if (const toml::node* n = t.get("address")) o.address = read_string(*n);
    if (const toml::node* n = t.get("client_name")) o.client_name = read_string(*n);
}

Calibration read_calibration(const toml::table& t) {
    check_keys(t, {"device", "zone", "gain"});
    Calibration c;
    const toml::node* device = t.get("device");
    if (!device) fail_at(t.source(), "missing field `device`");
    c.device = read_string(*device);
    if (const toml::node* zone = t.get("zone")) c.zone = read_string(*zone);

    const toml::node* gain = t.get("gain");
    if (!gain) fail_at(t.source(), "missing field `gain`");
    if (!gain->is_array()) fail_at(gain->source(), "invalid type: expected an array of length 3");
    const toml::array& values = *gain->as_array();
    if (values.size() != 3) {
        fail_at(gain->source(), std::format("invalid length {}, expected an array of length 3", values.size()));
    }
    for (int i = 0; i < 3; ++i) c.gain[i] = read_float(values[i]);
    return c;
}

}  // namespace

Config Config::from_toml(std::string_view text) {
    toml::table root;
    try {
        root = toml::parse(text);
    } catch (const toml::parse_error& e) {
        fail_at(e.source(), std::string(e.description()));
    }

    Config config;
    check_keys(root, {"led", "extract", "openrgb", "calibration"});
    if (const toml::node* n = root.get("led")) read_led(as_table(*n), config.led);
    if (const toml::node* n = root.get("extract")) read_extract(as_table(*n), config.extract);
    if (const toml::node* n = root.get("openrgb")) read_openrgb(as_table(*n), config.openrgb);
    if (const toml::node* n = root.get("calibration")) {
        if (!n->is_array()) fail_at(n->source(), "invalid type: expected a sequence");
        for (const toml::node& entry : *n->as_array()) {
            config.calibration.push_back(read_calibration(as_table(entry)));
        }
    }
    return config;
}

void check_toml(std::string_view text) {
    try {
        [[maybe_unused]] toml::table parsed = toml::parse(text);
    } catch (const toml::parse_error& e) {
        fail_at(e.source(), std::string(e.description()));
    }
}

// ---------------------------------------------------------------------------------------------
// Writing, in the same layout as the Rust version (toml::to_string_pretty).

/// Shortest form that reads back the same, never an exponent, always with a decimal point
/// (0.7, 1.0, 100000000000000000000.0).
std::string toml_float(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    char buffer[400];
    auto [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, v, std::chars_format::fixed);
    std::string out(buffer, end);
    if (out.find('.') == std::string::npos) out += ".0";
    return out;
}

std::string toml_string(std::string_view s) {
    std::string out = "\"";
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    out += std::format("\\u{:04X}", c);
                } else {
                    out += ch;
                }
        }
    }
    return out + "\"";
}

std::string Config::to_toml() const {
    std::string out;
    out += "[led]\n";
    out += std::format("mode = {}\n", toml_string(mode_name(led.mode)));
    out += "\n[extract]\n";
    out += std::format("palette_size = {}\n", extract.palette_size);
    out += std::format("sample_size = {}\n", extract.sample_size);
    out += std::format("min_saturation = {}\n", toml_float(extract.min_saturation));
    out += std::format("min_chroma = {}\n", toml_float(extract.min_chroma));
    out += std::format("min_share = {}\n", toml_float(extract.min_share));
    out += std::format("skin_weight = {}\n", toml_float(extract.skin_weight));
    out += std::format("pastel_gain = {}\n", toml_float(extract.pastel_gain));
    out += "\n[openrgb]\n";
    out += std::format("address = {}\n", toml_string(openrgb.address));
    out += std::format("client_name = {}\n", toml_string(openrgb.client_name));
    for (const Calibration& c : calibration) {
        out += "\n[[calibration]]\n";
        out += std::format("device = {}\n", toml_string(c.device));
        if (c.zone) out += std::format("zone = {}\n", toml_string(*c.zone));
        out += "gain = [\n";
        for (double g : c.gain) out += std::format("    {},\n", toml_float(g));
        out += "]\n";
    }
    return out;
}

}  // namespace wpc
