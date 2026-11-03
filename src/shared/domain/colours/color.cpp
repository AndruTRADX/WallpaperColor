#include "shared/domain/colours/color.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace wpc {

Hsv Rgb::to_hsv() const {
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;
    float max = std::max(std::max(rf, gf), bf);
    float min = std::min(std::min(rf, gf), bf);
    float delta = max - min;

    float h;
    if (delta == 0.0f) {
        h = 0.0f;
    } else if (max == rf) {
        h = 60.0f * rem_euclid((gf - bf) / delta, 6.0f);
    } else if (max == gf) {
        h = 60.0f * ((bf - rf) / delta + 2.0f);
    } else {
        h = 60.0f * ((rf - gf) / delta + 4.0f);
    }
    float s = (max == 0.0f) ? 0.0f : delta / max;
    return Hsv{h, s, max};
}

Rgb Rgb::from_hsv(Hsv hsv) {
    float c = hsv.v * hsv.s;
    float hp = rem_euclid(hsv.h, 360.0f) / 60.0f;
    float x = c * (1.0f - std::abs(std::fmod(hp, 2.0f) - 1.0f));

    float r1, g1, b1;
    switch (static_cast<uint32_t>(hp)) {
        case 0:  r1 = c;    g1 = x;    b1 = 0.0f; break;
        case 1:  r1 = x;    g1 = c;    b1 = 0.0f; break;
        case 2:  r1 = 0.0f; g1 = c;    b1 = x;    break;
        case 3:  r1 = 0.0f; g1 = x;    b1 = c;    break;
        case 4:  r1 = x;    g1 = 0.0f; b1 = c;    break;
        default: r1 = c;    g1 = 0.0f; b1 = x;    break;
    }

    float m = hsv.v - c;
    auto to8 = [m](float f) {
        return static_cast<uint8_t>(std::clamp(std::round((f + m) * 255.0f), 0.0f, 255.0f));
    };
    return Rgb{to8(r1), to8(g1), to8(b1)};
}

std::string Rgb::hex() const {
    return std::format("#{:02x}{:02x}{:02x}", r, g, b);
}

std::optional<uint8_t> parse_hex_byte(char a, char b) {
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int high = digit(a);
    int low = digit(b);
    if (high < 0 || low < 0) return std::nullopt;
    return static_cast<uint8_t>(high * 16 + low);
}

std::optional<Rgb> Rgb::parse(std::string_view text) {
    std::string_view hex = text;
    if (hex.starts_with('#')) hex.remove_prefix(1);
    if (hex.size() != 6) return std::nullopt;
    auto r = parse_hex_byte(hex[0], hex[1]);
    auto g = parse_hex_byte(hex[2], hex[3]);
    auto b = parse_hex_byte(hex[4], hex[5]);
    if (!r || !g || !b) return std::nullopt;
    return Rgb{*r, *g, *b};
}

std::string invalid_colour_message(std::string_view text) {
    return std::format("invalid colour \"{}\": expected #rrggbb or rrggbb", text);
}

float rem_euclid(float a, float b) {
    float r = std::fmod(a, b);
    return (r < 0.0f) ? r + std::abs(b) : r;
}

}  // namespace wpc
