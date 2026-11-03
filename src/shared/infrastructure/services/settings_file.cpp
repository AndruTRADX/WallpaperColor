#include "shared/infrastructure/services/settings_file.hpp"

#include "utils/toml.hpp"

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

#include "utils/error.hpp"

namespace wpc {

// ---------------------------------------------------------------------------------------------
// Paths and loading

std::optional<std::filesystem::path> SettingsFile::default_path() const {
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

Config SettingsFile::read(const std::filesystem::path& path) const {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (errno == ENOENT) return Config{};
        throw Error(std::format("{}: {}", path.string(), os_error(errno)));
    }
    std::stringstream text;
    text << file.rdbuf();
    try {
        return parse(text.str());
    } catch (const Error& e) {
        throw Error(std::format("{}: {}", path.string(), e.what()));
    }
}

// ---------------------------------------------------------------------------------------------
// Parsing. toml++ reads the file; the checks below make unknown keys and wrong types errors,
// so a typo is reported instead of silently doing nothing.

namespace {

/// An error about `node`, with its position in the file.
[[noreturn]] void fail_at(const toml::source_region& where, const std::string& message) {
    throw Error(std::format("TOML parse error at line {}, column {}: {}", where.begin.line,
                            where.begin.column, message));
}

/// "expected one of `a`, `b`, `c`".
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

bool read_bool(const toml::node& node) {
    if (!node.is_boolean()) fail_at(node.source(), "invalid type: expected true or false");
    return node.as_boolean()->get();
}

Mode read_mode(const toml::node& node) {
    std::string name = read_string(node);
    if (name == "arc") return Mode::Arc;
    if (name == "single") return Mode::Single;
    if (name == "split") return Mode::Split;
    fail_at(node.source(), std::format("unknown variant `{}`, expected one of `arc`, `single`, `split`", name));
}

void read_led(const toml::table& t, LedConfig& led) {
    check_keys(t, {"mode", "fade"});
    if (const toml::node* n = t.get("mode")) led.mode = read_mode(*n);
    if (const toml::node* n = t.get("fade")) led.fade = read_float(*n);
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

/// `[components]`: fans = true, ram = false…
void read_components(const toml::table& t, Config& config) {
    std::vector<std::string_view> names;
    for (Component c : ALL_COMPONENTS) names.push_back(component_name(c));
    check_keys(t, names);
    for (Component c : ALL_COMPONENTS) {
        if (const toml::node* n = t.get(component_name(c))) config.components.on[static_cast<size_t>(c)] = read_bool(*n);
    }
    config.components.chosen = true;
}

Component read_component(const toml::node& node) {
    std::string name = read_string(node);
    if (std::optional<Component> c = component_from_name(name)) return *c;
    std::vector<std::string_view> names;
    for (Component c : ALL_COMPONENTS) names.push_back(component_name(c));
    fail_at(node.source(), std::format("unknown component `{}`, {}", name, expected_list(names)));
}

ComponentRule read_component_rule(const toml::table& t) {
    check_keys(t, {"device", "zone", "is"});
    ComponentRule r;
    const toml::node* device = t.get("device");
    if (!device) fail_at(t.source(), "missing field `device`");
    r.device = read_string(*device);
    if (const toml::node* zone = t.get("zone")) r.zone = read_string(*zone);
    const toml::node* is = t.get("is");
    if (!is) fail_at(t.source(), "missing field `is`");
    r.is = read_component(*is);
    return r;
}

/// A driver's table, kept as plain values for the driver to read (domain/driver_settings.hpp).
DriverSettings read_driver_table(const std::string& name, const toml::table& t) {
    std::map<std::string, DriverSettings::Value> values;
    for (auto&& [key, node] : t) {
        DriverSettings::Value value;
        if (node.is_boolean()) {
            value = node.as_boolean()->get();
        } else if (node.is_string()) {
            value = node.as_string()->get();
        } else if (node.is_integer() || node.is_floating_point()) {
            value = read_float(node);
        } else if (node.is_array()) {
            std::vector<double> numbers;
            for (const toml::node& item : *node.as_array()) {
                if (!item.is_integer() && !item.is_floating_point()) fail_at(item.source(), "invalid type: expected a number");
                numbers.push_back(read_float(item));
            }
            value = std::move(numbers);
        } else {
            fail_at(node.source(), "invalid type: expected true/false, a number, a string or a list of numbers");
        }
        values.emplace(std::string(key.str()), std::move(value));
    }
    return DriverSettings(name, std::move(values));
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

Config SettingsFile::parse(std::string_view text) const {
    toml::table root;
    try {
        root = toml::parse(text);
    } catch (const toml::parse_error& e) {
        fail_at(e.source(), std::string(e.description()));
    }

    Config config;
    for (auto&& [key, node] : root) {
        std::string name(key.str());
        if (name == "led") {
            read_led(as_table(node), config.led);
        } else if (name == "extract") {
            read_extract(as_table(node), config.extract);
        } else if (name == "components") {
            read_components(as_table(node), config);
        } else if (name == "component" || name == "calibration") {
            if (!node.is_array()) fail_at(node.source(), "invalid type: expected a sequence");
            for (const toml::node& entry : *node.as_array()) {
                if (name == "component") config.components.rules.push_back(read_component_rule(as_table(entry)));
                else config.calibration.entries.push_back(read_calibration(as_table(entry)));
            }
        } else if (node.is_table()) {
            config.drivers.tables.emplace(name, read_driver_table(name, *node.as_table()));
        } else {
            fail_at(key.source(), std::format("unknown field `{}`: expected a table like [led] or a driver's [{}]", name, name));
        }
    }

    // Before [components] existed, `[motherboard] enabled = false` turned the board off.
    // Still honoured while the file has no [components] table.
    auto board = config.drivers.tables.find("motherboard");
    if (!config.components.chosen && board != config.drivers.tables.end() &&
        board->second.values().contains("enabled") &&
        board->second.values().at("enabled") == DriverSettings::Value(false)) {
        config.components.on[static_cast<size_t>(Component::Motherboard)] = false;
        config.components.on[static_cast<size_t>(Component::Argb)] = false;
    }
    return config;
}

// ---------------------------------------------------------------------------------------------
// Writing

/// A driver setting as TOML. Whole numbers are written without ".0" (argb_header1 = [8, 8]).
static std::string toml_value(const DriverSettings::Value& value) {
    auto number = [](double v) { return v == std::floor(v) && std::abs(v) < 1e15 ? std::format("{}", static_cast<int64_t>(v)) : toml_float(v); };
    if (const bool* b = std::get_if<bool>(&value)) return *b ? "true" : "false";
    if (const double* d = std::get_if<double>(&value)) return number(*d);
    if (const std::string* s = std::get_if<std::string>(&value)) return toml_string(*s);
    std::string out = "[";
    for (double v : std::get<std::vector<double>>(value)) out += (out.size() > 1 ? ", " : "") + number(v);
    return out + "]";
}

std::string SettingsFile::write(const Config& config) const {
    std::string out;
    out += "[led]\n";
    out += std::format("mode = {}\n", toml_string(mode_name(config.led.mode)));
    out += std::format("fade = {}\n", toml_float(config.led.fade));
    out += "\n[extract]\n";
    out += std::format("palette_size = {}\n", config.extract.palette_size);
    out += std::format("sample_size = {}\n", config.extract.sample_size);
    out += std::format("min_saturation = {}\n", toml_float(config.extract.min_saturation));
    out += std::format("min_chroma = {}\n", toml_float(config.extract.min_chroma));
    out += std::format("min_share = {}\n", toml_float(config.extract.min_share));
    out += std::format("skin_weight = {}\n", toml_float(config.extract.skin_weight));
    out += std::format("pastel_gain = {}\n", toml_float(config.extract.pastel_gain));
    if (config.components.chosen) {
        out += "\n[components]\n";
        for (Component c : ALL_COMPONENTS) out += std::format("{} = {}\n", component_name(c), config.components.wants(c));
    } else {
        out += "\n# [components] is not chosen yet: everything is painted (the Drivers page writes it)\n";
    }
    for (const ComponentRule& r : config.components.rules) {
        out += "\n[[component]]\n";
        out += std::format("device = {}\n", toml_string(r.device));
        if (r.zone) out += std::format("zone = {}\n", toml_string(*r.zone));
        out += std::format("is = {}\n", toml_string(component_name(r.is)));
    }
    for (const auto& [name, table] : config.drivers.tables) {
        out += std::format("\n[{}]\n", name);
        for (const auto& [key, value] : table.values()) out += std::format("{} = {}\n", key, toml_value(value));
    }
    for (const Calibration& c : config.calibration.entries) {
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
