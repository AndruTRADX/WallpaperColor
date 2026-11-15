#include "colours/colour.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace wpc {

namespace {

/// `f` (nominally 0-1) as a channel 0-255; NaN counts as 0, so no value is undefined.
uint8_t to_channel(float f) {
    float scaled = std::round(f * 255.0f);
    if (!(scaled > 0.0f)) return 0;  // also NaN
    return scaled >= 255.0f ? 255 : static_cast<uint8_t>(scaled);
}

/// `v` limited to 0-1; NaN counts as 0.
float unit(float v) {
    return v > 0.0f ? std::min(v, 1.0f) : 0.0f;
}

}  // namespace

Hsv to_hsv(Rgb c) {
    float rf = c.r / 255.0f;
    float gf = c.g / 255.0f;
    float bf = c.b / 255.0f;
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

Rgb to_rgb(Hsv hsv) {
    float v = unit(hsv.v);
    float c = v * unit(hsv.s);
    float hue = std::isfinite(hsv.h) ? rem_euclid(hsv.h, 360.0f) : 0.0f;
    float hp = hue / 60.0f;
    float x = c * (1.0f - std::abs(std::fmod(hp, 2.0f) - 1.0f));

    float r1, g1, b1;
    switch (std::min(static_cast<uint32_t>(hp), 5u)) {
        case 0:  r1 = c;    g1 = x;    b1 = 0.0f; break;
        case 1:  r1 = x;    g1 = c;    b1 = 0.0f; break;
        case 2:  r1 = 0.0f; g1 = c;    b1 = x;    break;
        case 3:  r1 = 0.0f; g1 = x;    b1 = c;    break;
        case 4:  r1 = x;    g1 = 0.0f; b1 = c;    break;
        default: r1 = c;    g1 = 0.0f; b1 = x;    break;
    }
    float m = v - c;
    return Rgb{to_channel(r1 + m), to_channel(g1 + m), to_channel(b1 + m)};
}

std::string to_hex(Rgb c) {
    return std::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b);
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

std::optional<Rgb> parse_colour(std::string_view text) {
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

// ---------------------------------------------------------------------------------------------
// Gradients

std::vector<Rgb> gradient(const std::vector<Rgb>& stops, size_t n, bool closed) {
    if (n == 0) return {};
    if (stops.empty()) return std::vector<Rgb>(n, Rgb{});
    if (stops.size() == 1) return std::vector<Rgb>(n, stops[0]);

    size_t segments = closed ? stops.size() : stops.size() - 1;
    std::vector<Rgb> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        float position;  // 0..1 along the row or ring
        if (closed) {
            position = static_cast<float>(i) / static_cast<float>(n);
        } else if (n == 1) {
            position = 0.0f;
        } else {
            position = static_cast<float>(i) / static_cast<float>(n - 1);
        }
        float t = position * static_cast<float>(segments);
        size_t seg = std::min(static_cast<size_t>(std::floor(t)), segments - 1);
        Rgb a = stops[seg];
        Rgb b = stops[(seg + 1) % stops.size()];
        out.push_back(mix(a, b, t - static_cast<float>(seg)));
    }
    return out;
}

Rgb mix(Rgb a, Rgb b, float t) {
    Hsv ha = to_hsv(a);
    Hsv hb = to_hsv(b);
    float hue_a = (ha.s == 0.0f) ? hb.h : ha.h;
    float hue_b = (hb.s == 0.0f) ? hue_a : hb.h;

    float dh = hue_b - hue_a;  // the shorter way round
    if (dh > 180.0f) {
        dh -= 360.0f;
    } else if (dh < -180.0f) {
        dh += 360.0f;
    }
    return to_rgb(Hsv{
        rem_euclid(hue_a + dh * t, 360.0f),
        ha.s + (hb.s - ha.s) * t,
        ha.v + (hb.v - ha.v) * t,
    });
}

}  // namespace wpc
