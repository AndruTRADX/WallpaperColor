#include "shared/domain/colours/paint.hpp"

#include <algorithm>
#include <cmath>

#include "shared/domain/colours/gradient.hpp"

namespace wpc {

Painter::Painter(std::vector<Rgb> colours, Mode mode, const Calibrations& calibrations)
    : colours_(std::move(colours)), mode_(mode), calibrations_(calibrations) {
    if (colours_.empty()) colours_.push_back(WHITE);
}

std::vector<Rgb> Painter::zone(std::string_view device, std::string_view zone, uint32_t leds, Shape shape) {
    std::vector<Rgb> out;
    switch (mode_) {
        case Mode::Arc:
            out = gradient(colours_, leds, shape == Shape::Ring);
            break;
        case Mode::Single:
            out.assign(leds, colours_[0]);
            break;
        case Mode::Split: {
            auto known = std::find(zone_names_.begin(), zone_names_.end(), zone);
            size_t position = static_cast<size_t>(known - zone_names_.begin());
            if (known == zone_names_.end()) zone_names_.emplace_back(zone);
            out.assign(leds, colours_[position % colours_.size()]);
            break;
        }
    }
    std::array<float, 3> gain = calibrations_.gain(device, zone);
    for (Rgb& c : out) c = scale(c, gain);
    return out;
}

Rgb scale(Rgb c, std::array<float, 3> gain) {
    auto s = [](uint8_t v, float k) {
        return static_cast<uint8_t>(std::clamp(std::round(static_cast<float>(v) * k), 0.0f, 255.0f));
    };
    return Rgb{s(c.r, gain[0]), s(c.g, gain[1]), s(c.b, gain[2])};
}

}  // namespace wpc
