#include "daemon/service/rules.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>

#include "colours/colour.hpp"

namespace wpc {

// ---------------------------------------------------------------------------------------------
// Outputs

void plan_outputs(const std::string& driver, const std::vector<Device>& devices, const ComponentSettings& components,
                  Painter& painter, std::vector<Output>& outputs, std::vector<ipc::DeviceInfo>& found) {
    for (size_t index = 0; index < devices.size(); ++index) {
        const Device& d = devices[index];
        ipc::DeviceInfo info{d.name, driver, {}};
        Output o{driver, index, driver + " " + d.key, {}, {}, {}};
        size_t first = 0;  // the zone's first LED in d.reported
        for (size_t z = 0; z < d.zones.size(); ++z) {
            const Zone& zone = d.zones[z];
            Component c = components.component_of(d.name, zone.name, zone.component);
            info.zones.push_back(ipc::ZoneInfo{zone.name, zone.leds, c, zone.shape});
            if (components.wants(c)) {
                std::vector<Rgb> leds = painter.zone(d.name, zone.name, zone.leds, zone.shape);
                o.zones.emplace_back(z, leds.size());
                o.target.insert(o.target.end(), leds.begin(), leds.end());
                if (d.reported.size() >= first + zone.leds) {
                    auto from = d.reported.begin() + static_cast<std::ptrdiff_t>(first);
                    o.reported.insert(o.reported.end(), from, from + zone.leds);
                }
            }
            first += zone.leds;
        }
        found.push_back(std::move(info));
        if (o.zones.empty()) continue;
        if (o.reported.size() != o.target.size()) o.reported.clear();
        outputs.push_back(std::move(o));
    }
}

// ---------------------------------------------------------------------------------------------
// Fades

int fade_frames(double seconds, bool changes) {
    if (!changes || !(seconds > 0.0)) return 1;  // also NaN
    seconds = std::min(seconds, ranges::FADE.max);
    return std::max(1, static_cast<int>(std::round(seconds * FADE_FPS)));
}

const std::vector<Rgb>& fade_start(const std::vector<Rgb>* last_sent, const std::vector<Rgb>& reported,
                                   const std::vector<Rgb>& target) {
    if (last_sent && last_sent->size() == target.size()) return *last_sent;
    if (reported.size() == target.size()) return reported;
    return target;
}

std::vector<Rgb> fade_frame(const std::vector<Rgb>& from, const std::vector<Rgb>& to, int frame, int frames) {
    std::vector<Rgb> out(to.size());
    fade_frame_into(from, to, frame, frames, out);
    return out;
}

bool fade_frame_into(std::span<const Rgb> from, std::span<const Rgb> to, int frame, int frames, std::span<Rgb> out) {
    float t = 1.0f;
    if (frame < frames) {
        t = static_cast<float>(frame) / static_cast<float>(frames);
        t = t * t * (3.0f - 2.0f * t);  // ease in and out
    }
    bool changed = false;
    for (size_t led = 0; led < out.size(); ++led) {
        Rgb c = frame < frames ? mix(from[led], to[led], t) : to[led];
        changed = changed || c != out[led];
        out[led] = c;
    }
    return changed;
}

// ---------------------------------------------------------------------------------------------
// Retries

RetryDelay next_retry_delay(std::optional<RetryDelay> last) {
    using namespace std::chrono_literals;
    if (!last) return 2s;
    return std::min<RetryDelay>(*last * 2, 60s);
}

// ---------------------------------------------------------------------------------------------
// Drivers and pictures

bool driver_needed(const std::vector<Component>& paints, const ComponentSettings& chosen) {
    return std::any_of(paints.begin(), paints.end(), [&](Component c) { return chosen.wants(c); });
}

std::vector<Component> unpainted(const std::vector<std::vector<Component>>& installed, const ComponentSettings& chosen) {
    std::vector<Component> out;
    for (Component c : ALL_COMPONENTS) {
        if (!chosen.wants(c)) continue;
        bool painted = std::any_of(installed.begin(), installed.end(), [&](const std::vector<Component>& paints) {
            return std::find(paints.begin(), paints.end(), c) != paints.end();
        });
        if (!painted) out.push_back(c);
    }
    return out;
}

bool is_picture_name(std::string_view name) {
    constexpr std::array<std::string_view, 6> extensions = {"png", "jpg", "jpeg", "webp", "gif", "bmp"};
    std::string ext = std::filesystem::path(name).extension().string();
    if (!ext.empty()) ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

}  // namespace wpc
