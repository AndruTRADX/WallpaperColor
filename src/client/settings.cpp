#include "client/settings.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "util/error.hpp"

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

}  // namespace

// ---------------------------------------------------------------------------------------------

Settings Settings::from_text(std::string_view text) {
    Settings s;
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

Settings Settings::load(const fs::path& path) {
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
    Settings s = from_text(text);
    s.path_ = path;
    return s;
}

std::string Settings::text() const {
    std::string out;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (i > 0) out += '\n';
        out += lines_[i];
    }
    return out;
}

Config Settings::config() const {
    try {
        return Config::from_toml(text());
    } catch (const Error&) {
        return Config{};
    }
}

bool Settings::set_mode(Mode mode) {
    return set_value("led", "mode", toml_string(mode_name(mode)));
}

bool Settings::set_intensity(double value) {
    return set_value("extract", "min_saturation", toml_float(round2(std::clamp(value, 0.2, 1.0))));
}

bool Settings::replace_value(size_t line, size_t value_start, const std::string& value) {
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

bool Settings::set_value(std::string_view table, std::string_view key, const std::string& value) {
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
    bool ends_with_newline = !lines_.empty() && lines_.back().empty();
    if (ends_with_newline) lines_.pop_back();
    if (!lines_.empty() && !trim(lines_.back()).empty()) lines_.push_back("");
    lines_.push_back(std::format("[{}]", table));
    lines_.push_back(line);
    lines_.push_back("");
    return true;
}

bool Settings::set_gain(size_t index, int channel, double value) {
    size_t seen = 0;
    std::optional<size_t> header;     // line of the index-th [[calibration]]
    std::optional<KeyLine> gain_key;  // its gain line
    size_t gain_line = 0;
    std::optional<size_t> last_key;
    bool in_entry = false;

    for (size_t i = 0; i < lines_.size(); ++i) {
        if (std::optional<Header> h = header_of(lines_[i])) {
            if (in_entry) break;  // the entry ended
            if (h->array && h->name == "calibration") {
                if (seen == index) {
                    header = i;
                    in_entry = true;
                }
                ++seen;
            }
            continue;
        }
        std::optional<KeyLine> k = key_line_of(lines_[i]);
        if (!k) continue;
        if (!header && seen == 0 && k->key == "calibration") return false;  // `calibration = [{…}]`
        if (in_entry && k->key == "gain") {
            gain_key = k;
            gain_line = i;
        }
        i = value_last_line(lines_, i, k->value_start);
        if (in_entry) last_key = i;
    }
    if (!header) return true;  // no such entry: nothing to change

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
    lines_.insert(lines_.begin() + static_cast<long>(last_key.value_or(*header)) + 1, "gain = " + array);
    return true;
}

void Settings::save() const {
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
