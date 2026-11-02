#include "util/utf8.hpp"

#include <cstdint>

namespace wpc {

/// Length of the valid UTF-8 sequence at the start of `s`, or 0 if it is invalid there.
/// When it is invalid, `bad` is how many bytes make up the invalid part (at least 1): the
/// "maximal subpart" rule Rust follows, so a cut-off sequence gives a single U+FFFD.
static size_t valid_sequence(std::string_view s, size_t& bad) {
    auto byte = [&](size_t i) { return static_cast<uint8_t>(s[i]); };
    uint8_t first = byte(0);
    bad = 1;
    if (first < 0x80) return 1;

    size_t length;
    uint8_t lo = 0x80;  // allowed range of the second byte
    uint8_t hi = 0xBF;
    if (first >= 0xC2 && first <= 0xDF) {
        length = 2;
    } else if (first >= 0xE0 && first <= 0xEF) {
        length = 3;
        if (first == 0xE0) lo = 0xA0;  // no overlong forms
        if (first == 0xED) hi = 0x9F;  // no surrogates
    } else if (first >= 0xF0 && first <= 0xF4) {
        length = 4;
        if (first == 0xF0) lo = 0x90;
        if (first == 0xF4) hi = 0x8F;  // nothing above U+10FFFF
    } else {
        return 0;
    }

    for (size_t i = 1; i < length; ++i) {
        if (i >= s.size()) return 0;
        uint8_t b = byte(i);
        uint8_t min = (i == 1) ? lo : 0x80;
        uint8_t max = (i == 1) ? hi : 0xBF;
        if (b < min || b > max) return 0;
        bad = i + 1;
    }
    return length;
}

std::string utf8_lossy(std::string_view bytes) {
    std::string out;
    while (!bytes.empty()) {
        size_t bad = 1;
        size_t n = valid_sequence(bytes, bad);
        if (n > 0) {
            out.append(bytes.substr(0, n));
            bytes.remove_prefix(n);
        } else {
            out += "\xEF\xBF\xBD";  // U+FFFD
            bytes.remove_prefix(bad);
        }
    }
    return out;
}

bool is_valid_utf8(std::string_view text) {
    while (!text.empty()) {
        size_t bad = 1;
        size_t n = valid_sequence(text, bad);
        if (n == 0) return false;
        text.remove_prefix(n);
    }
    return true;
}

}  // namespace wpc
