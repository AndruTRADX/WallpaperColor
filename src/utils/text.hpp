// cat(): joins strings and numbers into one string, cat("LED ", 3, " of ", name).
//
// Driver modules use it instead of std::format: std::format adds about 70 KB of code to every
// module that uses it, and each module is its own file in memory. The programs, which carry
// std::format once anyway, keep using it.

#pragma once

#include <charconv>
#include <concepts>
#include <string>
#include <string_view>

namespace wpc {

namespace text_detail {

inline void append(std::string& out, std::string_view s) { out += s; }
inline void append(std::string& out, char c) { out += c; }

template <std::integral T>
    requires(!std::same_as<T, char> && !std::same_as<T, bool>)
void append(std::string& out, T value) {
    out += std::to_string(value);
}

inline void append(std::string& out, double value) {
    char buffer[32];
    auto [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, value);
    out.append(buffer, end);
}

}  // namespace text_detail

template <typename... Parts>
std::string cat(const Parts&... parts) {
    std::string out;
    (text_detail::append(out, parts), ...);
    return out;
}

/// Whether `hay` contains `needle`, ignoring case ("fan" is in "8-LED Series Fan"). How names
/// in the config match device and zone names.
inline bool contains_ignoring_case(std::string_view hay, std::string_view needle) {
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    if (needle.size() > hay.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        size_t j = 0;
        while (j < needle.size() && lower(hay[i + j]) == lower(needle[j])) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}

}  // namespace wpc
