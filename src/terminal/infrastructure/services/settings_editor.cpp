#include "terminal/infrastructure/services/settings_editor.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "utils/error.hpp"
#include "utils/toml.hpp"

namespace wpc {

namespace fs = std::filesystem;

namespace {

std::string_view trim(std::string_view s) {
    const char* space = " \t\r";
    size_t start = s.find_first_not_of(space);
    if (start == std::string_view::npos) return {};
    return s.substr(start, s.find_last_not_of(space) - start + 1);
}

double round2(double v) {
    return std::round(v * 100.0) / 100.0;
}

/// A table header line: `[name]` or `[[name]]` (for an array of tables).
struct Header {
    std::string name;
    bool array = false;
};

std::optional<Header> header_of(std::string_view line) {
    std::string_view t = trim(line);
    if (!t.starts_with('[')) return std::nullopt;
    bool array = t.starts_with("[[");
    std::string_view close = array ? "]]" : "]";
    size_t open_size = array ? 2 : 1;
    size_t end = t.find(close, open_size);
    if (end == std::string_view::npos) return std::nullopt;
    return Header{std::string(trim(t.substr(open_size, end - open_size))), array};
}

/// A `key = value  # comment` line: the key and where its value starts.
struct KeyLine {
    std::string key;
    size_t value_start = 0;
};

std::optional<KeyLine> key_line_of(std::string_view line) {
    std::string_view t = trim(line);
    if (t.empty() || t.starts_with('#') || t.starts_with('[')) return std::nullopt;
    size_t equals = line.find('=');
    if (equals == std::string_view::npos) return std::nullopt;
    size_t start = line.find_first_not_of(" \t", equals + 1);
    return KeyLine{std::string(trim(line.substr(0, equals))), start == std::string_view::npos ? line.size() : start};
}

/// Where a value ends, and its text without comments. Arrays may go on over several lines.
struct ValueSpan {
    size_t last_line = 0;  // line of the value's last character
    size_t end = 0;        // column just past it (a trailing comment starts after this)
    std::string text;      // the value, comments removed, lines joined with spaces
};

/// The value starting at `start` in `lines[line]`; nullopt for """ strings (not edited).
std::optional<ValueSpan> value_span(const std::vector<std::string>& lines, size_t line, size_t start) {
    std::string_view first = std::string_view(lines[line]).substr(start);
    if (first.starts_with("\"\"\"") || first.starts_with("'''")) return std::nullopt;

    ValueSpan span{line, start, ""};
    char quote = 0;
    int depth = 0;
    for (size_t l = line; l < lines.size(); ++l) {
        const std::string& text = lines[l];
        for (size_t i = (l == line ? start : 0); i < text.size(); ++i) {
            char c = text[i];
            if (!quote && c == '#') break;  // the rest of the line is a comment
            span.text += c;
            if (c != ' ' && c != '\t') {
                span.last_line = l;
                span.end = i + 1;
            }
            if (quote) {
                if (c == '\\' && quote == '"' && i + 1 < text.size()) span.text += text[++i];
                else if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '[' || c == '{') {
                ++depth;
            } else if (c == ']' || c == '}') {
                --depth;
            }
        }
        if (depth <= 0 && !quote) return span;
        span.text += ' ';
    }
    return std::nullopt;  // never closed
}

/// The numbers in an array like `[0.5, 1, 0.9]` (anything else is skipped).
std::vector<double> numbers_of(std::string_view value) {
    std::vector<double> out;
    value = trim(value);
    if (!value.starts_with('[') || !value.ends_with(']')) return out;
    std::string_view inside = value.substr(1, value.size() - 2);
    while (!inside.empty()) {
        size_t comma = inside.find(',');
        std::string item(trim(inside.substr(0, comma)));
        char* end = nullptr;
        double v = std::strtod(item.c_str(), &end);
        if (!item.empty() && end == item.c_str() + item.size()) out.push_back(v);
        if (comma == std::string_view::npos) break;
        inside.remove_prefix(comma + 1);
    }
    return out;
}

/// Makes room for a new table at the end: drops the "" after a final newline, then leaves one
/// blank line after the last text.
void start_table_at_end(std::vector<std::string>& lines) {
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
}

}  // namespace

// ---------------------------------------------------------------------------------------------

TomlSettingsEditor TomlSettingsEditor::from_text(std::string_view text) {
    TomlSettingsEditor s;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            s.lines_.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    s.lines_.push_back(current);  // "" when the text ends with a newline
    return s;
}

TomlSettingsEditor TomlSettingsEditor::open(const fs::path& path) {
    std::string text;
    std::ifstream file(path, std::ios::binary);
    if (file) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        text = buffer.str();
    } else if (errno != ENOENT) {
        throw Error(os_error(errno));
    }
    check_toml(text);
    TomlSettingsEditor s = from_text(text);
    s.path_ = path;
    return s;
}

std::string TomlSettingsEditor::text() const {
    std::string out;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (i > 0) out += '\n';
        out += lines_[i];
    }
    return out;
}

bool TomlSettingsEditor::set_mode(Mode mode) {
    return set_value("led", "mode", toml_string(mode_name(mode)));
}

bool TomlSettingsEditor::set_components(const std::array<bool, ALL_COMPONENTS.size()>& chosen) {
    // Every component is written, so the table says everything (and `[motherboard] enabled`
    // from before [components] no longer matters).
    for (Component c : ALL_COMPONENTS) {
        if (!set_value("components", component_name(c), chosen[static_cast<size_t>(c)] ? "true" : "false")) return false;
    }
    return true;
}

bool TomlSettingsEditor::set_float(std::string_view table, std::string_view key, double value, int decimals) {
    double scale = std::pow(10.0, decimals);
    return set_value(table, key, toml_float(std::round(value * scale) / scale));
}

bool TomlSettingsEditor::set_integer(std::string_view table, std::string_view key, long value) {
    return set_value(table, key, std::to_string(value));
}

bool TomlSettingsEditor::set_string(std::string_view table, std::string_view key, std::string_view value) {
    return set_value(table, key, toml_string(value));
}

bool TomlSettingsEditor::replace_value(size_t line, size_t value_start, const std::string& value) {
    std::optional<ValueSpan> span = value_span(lines_, line, value_start);
    if (!span) return false;
    std::string rest = lines_[span->last_line].substr(span->end);  // e.g. "  # comment"
    lines_[line] = lines_[line].substr(0, value_start) + value + rest;
    lines_.erase(lines_.begin() + static_cast<long>(line) + 1, lines_.begin() + static_cast<long>(span->last_line) + 1);
    return true;
}

/// The last line of the value of the key on line `line` (the same line unless it is a
/// multi-line array).
static size_t value_last_line(const std::vector<std::string>& lines, size_t line, size_t value_start) {
    std::optional<ValueSpan> span = value_span(lines, line, value_start);
    return span ? span->last_line : line;
}

bool TomlSettingsEditor::set_value(std::string_view table, std::string_view key, const std::string& value) {
    std::string current;  // table of the line being read ("" before the first header)
    bool current_is_array = false;
    std::optional<size_t> header;    // line of [table]
    std::optional<size_t> last_key;  // last line of the last key inside [table]

    for (size_t i = 0; i < lines_.size(); ++i) {
        if (std::optional<Header> h = header_of(lines_[i])) {
            current = h->name;
            current_is_array = h->array;
            if (!header && current == table && !current_is_array) header = i;
            continue;
        }
        std::optional<KeyLine> k = key_line_of(lines_[i]);
        if (!k) continue;

        // `led.mode = …` or `led = { mode = … }` at the top: a layout we do not edit.
        if (current.empty() && (k->key == table || k->key.starts_with(std::string(table) + "."))) return false;

        if (header && current == table && !current_is_array && k->key == key) {
            return replace_value(i, k->value_start, value);
        }
        i = value_last_line(lines_, i, k->value_start);  // skip the rest of a multi-line value
        if (header && current == table && !current_is_array) last_key = i;
    }

    std::string line = std::format("{} = {}", key, value);
    if (header) {
        // the key is missing from the table: add it after its last key
        lines_.insert(lines_.begin() + static_cast<long>(last_key.value_or(*header)) + 1, line);
        return true;
    }

    // the table is missing: add it at the end, after a blank line
    start_table_at_end(lines_);
    lines_.push_back(std::format("[{}]", table));
    lines_.push_back(line);
    lines_.push_back("");
    return true;
}

/// Line of the `index`-th `[[calibration]]` header, and the line where its entry ends
/// (the next header, or the end). nullopt if there is no such entry.
static std::optional<std::pair<size_t, size_t>> calibration_block(const std::vector<std::string>& lines,
                                                                   size_t index) {
    size_t seen = 0;
    std::optional<size_t> start;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::optional<Header> h = header_of(lines[i]);
        if (!h) continue;
        if (start) return std::pair{*start, i};
        if (h->array && h->name == "calibration" && seen++ == index) start = i;
    }
    if (start) return std::pair{*start, lines.size()};
    return std::nullopt;
}

/// Whether the file writes calibrations as `calibration = [{…}]` (a layout we do not edit).
static bool inline_calibrations(const std::vector<std::string>& lines) {
    for (const std::string& line : lines) {
        if (header_of(line)) return false;  // only keys before the first table can be that
        std::optional<KeyLine> k = key_line_of(line);
        if (k && k->key == "calibration") return true;
    }
    return false;
}

bool TomlSettingsEditor::set_gain(size_t index, int channel, double value) {
    if (inline_calibrations(lines_)) return false;
    std::optional<std::pair<size_t, size_t>> block = calibration_block(lines_, index);
    if (!block) return true;  // no such entry: nothing to change
    auto [header, end] = *block;

    std::optional<KeyLine> gain_key;  // the entry's gain line
    size_t gain_line = 0;
    size_t last_key = header;  // last line of the entry's last key
    for (size_t i = header + 1; i < end; ++i) {
        std::optional<KeyLine> k = key_line_of(lines_[i]);
        if (!k) continue;
        if (k->key == "gain") {
            gain_key = k;
            gain_line = i;
        }
        i = last_key = value_last_line(lines_, i, k->value_start);
    }

    std::vector<double> gain;
    if (gain_key) {
        std::optional<ValueSpan> span = value_span(lines_, gain_line, gain_key->value_start);
        if (!span) return false;
        gain = numbers_of(span->text);
    }
    gain.resize(3, 1.0);
    gain[channel] = round2(std::clamp(value, 0.0, 1.0));
    std::string array = std::format("[{}, {}, {}]", toml_float(gain[0]), toml_float(gain[1]), toml_float(gain[2]));

    if (gain_key) return replace_value(gain_line, gain_key->value_start, array);  // keeps "# red, green, blue"
    lines_.insert(lines_.begin() + static_cast<long>(last_key) + 1, "gain = " + array);
    return true;
}

bool TomlSettingsEditor::add_calibration(std::string_view device, std::optional<std::string_view> zone) {
    if (inline_calibrations(lines_)) return false;
    start_table_at_end(lines_);
    lines_.push_back("[[calibration]]");
    lines_.push_back("device = " + toml_string(device));
    if (zone) lines_.push_back("zone = " + toml_string(*zone));
    lines_.push_back("gain = [1.0, 1.0, 1.0]  # red, green, blue");
    lines_.push_back("");
    return true;
}

bool TomlSettingsEditor::remove_calibration(size_t index) {
    if (inline_calibrations(lines_)) return false;
    std::optional<std::pair<size_t, size_t>> block = calibration_block(lines_, index);
    if (!block) return true;  // no such entry: nothing to change
    auto [start, end] = *block;

    // Comments right above the header belong to this entry; comments and blank lines at the
    // end of it belong to the next one.
    while (start > 0 && trim(lines_[start - 1]).starts_with('#')) --start;
    if (end < lines_.size()) {
        while (end > start && (trim(lines_[end - 1]).empty() || trim(lines_[end - 1]).starts_with('#'))) --end;
    }
    lines_.erase(lines_.begin() + static_cast<long>(start), lines_.begin() + static_cast<long>(end));

    // Don't leave two blank lines in a row where the entry was.
    if (start > 0 && start < lines_.size() && trim(lines_[start - 1]).empty() && trim(lines_[start]).empty()) {
        lines_.erase(lines_.begin() + static_cast<long>(start));
    }
    return true;
}

void TomlSettingsEditor::save() const {
    try {
        if (path_.has_parent_path()) fs::create_directories(path_.parent_path());
    } catch (const fs::filesystem_error& e) {
        throw Error(os_error(e.code().value()));
    }
    fs::path tmp = path_;
    tmp.replace_extension("toml.tmp");
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file) throw Error(os_error(errno));
        file << text();
        if (!file.flush()) throw Error(os_error(errno));
    }
    std::error_code ec;
    fs::rename(tmp, path_, ec);
    if (ec) throw Error(os_error(ec.value()));
}

}  // namespace wpc
