#include "daemon/domain/fade.hpp"

#include <algorithm>
#include <cmath>

#include "shared/domain/colours/gradient.hpp"

namespace wpc {

int fade_frames(double seconds, bool changes) {
    if (!changes) return 1;
    seconds = std::clamp(seconds, 0.0, FADE_MAX_SECONDS);
    return std::max(1, static_cast<int>(std::round(seconds * FADE_FPS)));
}

const std::vector<Rgb>& fade_start(const std::vector<Rgb>* last_sent, const std::vector<Rgb>& reported,
                                   const std::vector<Rgb>& target) {
    if (last_sent && last_sent->size() == target.size()) return *last_sent;
    if (reported.size() == target.size()) return reported;
    return target;
}

std::vector<Rgb> fade_frame(const std::vector<Rgb>& from, const std::vector<Rgb>& to, int frame, int frames) {
    if (frame >= frames) return to;
    float t = static_cast<float>(frame) / static_cast<float>(frames);
    t = t * t * (3.0f - 2.0f * t);  // ease in and out
    std::vector<Rgb> out(to.size());
    for (size_t led = 0; led < out.size(); ++led) out[led] = mix(from[led], to[led], t);
    return out;
}

}  // namespace wpc
