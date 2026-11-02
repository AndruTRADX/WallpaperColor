#include "core/arc.hpp"

#include <algorithm>
#include <cmath>

namespace wpc {

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
    Hsv ha = a.to_hsv();
    Hsv hb = b.to_hsv();
    float hue_a = (ha.s == 0.0f) ? hb.h : ha.h;
    float hue_b = (hb.s == 0.0f) ? hue_a : hb.h;

    float dh = hue_b - hue_a;  // the shorter way round
    if (dh > 180.0f) {
        dh -= 360.0f;
    } else if (dh < -180.0f) {
        dh += 360.0f;
    }
    return Rgb::from_hsv(Hsv{
        rem_euclid(hue_a + dh * t, 360.0f),
        ha.s + (hb.s - ha.s) * t,
        ha.v + (hb.v - ha.v) * t,
    });
}

}  // namespace wpc
