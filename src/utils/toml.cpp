#include "utils/toml.hpp"

#include <charconv>
#include <cmath>
#include <format>

#include <toml++/toml.hpp>

#include "utils/error.hpp"

namespace wpc {

std::string toml_float(double v) {
    // Shortest form that reads back the same, never an exponent, always with a decimal point.
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

void check_toml(std::string_view text) {
    try {
        [[maybe_unused]] toml::table parsed = toml::parse(text);
    } catch (const toml::parse_error& e) {
        throw Error(std::format("TOML parse error at line {}, column {}: {}", e.source().begin.line,
                                e.source().begin.column, e.description()));
    }
}

}  // namespace wpc
