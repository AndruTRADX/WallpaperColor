#include "utils/url.hpp"

#include <string>

namespace wpc {

namespace {

/// One byte from two hex digits ("7f" → 127); nullopt if they are not hex digits.
std::optional<unsigned char> hex_byte(char a, char b) {
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int hi = digit(a);
    int lo = digit(b);
    if (hi < 0 || lo < 0) return std::nullopt;
    return static_cast<unsigned char>(hi * 16 + lo);
}

}  // namespace

std::optional<std::filesystem::path> url_to_path(std::string_view url) {
    if (url.empty() || url == "null") return std::nullopt;
    if (!url.starts_with("file://")) {
        if (url.starts_with('/')) return std::filesystem::path(url);
        return std::nullopt;
    }
    std::string_view rest = url.substr(7);
    std::string out;
    for (size_t i = 0; i < rest.size();) {
        if (rest[i] == '%' && i + 2 < rest.size()) {
            if (std::optional<unsigned char> b = hex_byte(rest[i + 1], rest[i + 2])) {
                out += static_cast<char>(*b);
                i += 3;
                continue;
            }
        }
        out += rest[i];
        i += 1;
    }
    return std::filesystem::path(out);
}

}  // namespace wpc
