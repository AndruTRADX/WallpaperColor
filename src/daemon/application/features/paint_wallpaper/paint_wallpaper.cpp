#include "daemon/application/features/paint_wallpaper/paint_wallpaper.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <span>

#include "daemon/domain/fade.hpp"
#include "daemon/domain/outputs.hpp"
#include "shared/domain/colours/extract.hpp"
#include "shared/domain/colours/paint.hpp"
#include "utils/error.hpp"
#include "utils/log.hpp"

namespace wpc {

namespace fs = std::filesystem;

namespace {

/// What decides the colours: the picture, its mtime, the mode and the colour settings.
std::string key_of(const fs::path& picture, std::optional<int64_t> mtime, const Config& config) {
    std::string mtime_text = mtime ? std::to_string(*mtime) : "none";
    const ExtractConfig& x = config.extract;
    return std::format("{} {} {} {} {} {} {} {} {} {}", picture.string(), mtime_text, mode_name(config.led.mode),
                       x.palette_size, x.sample_size, x.min_saturation, x.min_chroma, x.min_share, x.skin_weight,
                       x.pastel_gain);
}

/// Shows the fade's frames on every output, `FADE_FPS` a second; a driver that fails is left
/// out of the next frames, its problem in `failed`.
void fade(DriversState& d, DriverModules& drivers, Clock& clock, const std::vector<Output>& outputs, double seconds,
          std::map<std::string, std::string>& failed) {
    std::vector<std::vector<Rgb>> from;
    bool changes = false;
    for (const Output& o : outputs) {
        auto last = d.shown.find(o.key);
        from.push_back(fade_start(last != d.shown.end() ? &last->second : nullptr, o.reported, o.target));
        changes = changes || from.back() != o.target;
    }

    int frames = fade_frames(seconds, changes);
    TimePoint start = clock.now();
    for (int f = 1; f <= frames; ++f) {
        for (size_t i = 0; i < outputs.size(); ++i) {
            const Output& o = outputs[i];
            if (failed.contains(o.driver)) continue;
            std::vector<Rgb> frame = fade_frame(from[i], o.target, f, frames);
            std::vector<ZoneFrame> zones;
            size_t at = 0;
            for (auto [zone, count] : o.zones) {
                zones.push_back(ZoneFrame{zone, std::span<const Rgb>(frame).subspan(at, count)});
                at += count;
            }
            try {
                drivers.show(o.driver, o.device, zones);
            } catch (const Error& e) {
                failed[o.driver] = e.what();
            }
        }
        if (f < frames) clock.wait_until(start + f * std::chrono::milliseconds(1000 / FADE_FPS));
    }
    for (const Output& o : outputs) {
        if (failed.contains(o.driver)) {
            d.shown.erase(o.key);  // unknown now
        } else {
            d.shown[o.key] = o.target;
        }
    }
}

}  // namespace

void paint_wallpaper(ServiceState& state, DesktopWallpaper& desktop, WallpaperPackages& packages, ColourScheme& scheme,
                     Pictures& pictures, DriverModules& drivers, Clock& clock, Memory& memory, bool force,
                     const std::function<void(const fs::path&)>& showing) {
    WallpaperSettings on;
    try {
        on = desktop.current();
    } catch (const Error& e) {
        return schedule_retry(state, clock, std::string("cannot ask the desktop for the wallpaper: ") + e.what());
    }
    if (on.plugin.empty()) {
        // No desktop for now (the monitor went off and Plasma has no screen): no signal says when
        // it is back, so ask again. The slideshow folders stay watched.
        return schedule_retry(state, clock, "the desktop has no screen for now");
    }
    state.shown.plugin = on.plugin;
    state.slide_folders = on.slide_folders;
    std::optional<fs::path> found = on.image ? packages.picture(*on.image, scheme.is_dark()) : std::nullopt;
    if (!found) {
        state.retry.reset();
        return log("no picture to take colours from (plugin {})", on.plugin);
    }

    const fs::path& picture = *found;
    state.shown.image = picture;
    if (showing) showing(picture);
    std::string key = key_of(picture, pictures.modified_time(picture), state.config);
    if (!force && !state.retry && state.last_painted == key) return;

    if (state.extracted_key != key) {
        try {
            Picture read = pictures.read(picture, picture_side(state.config.extract));
            state.extracted_stops = extract(read, state.config.extract).stops(state.config.led.mode);
            memory.release();  // give the decoded picture's memory back to the system
        } catch (const Error& e) {
            memory.release();
            state.retry.reset();
            state.shown.error = std::format("{}: {}", picture.string(), e.what());
            return log("{}: {}", picture.string(), e.what());
        }
        state.extracted_key = key;
    }
    state.shown.stops.clear();
    for (Rgb c : state.extracted_stops) state.shown.stops.push_back(c.hex());
    if (state.paused) {
        state.last_painted.reset();  // paint on resume
        state.retry.reset();         // nothing to try again until then (a due retry would spin)
        return;
    }

    paint_colours(state, drivers, clock, state.extracted_stops, state.config.led.mode);
    if (state.retry) return;
    state.last_painted = key;
    std::string joined;
    for (const std::string& c : state.shown.stops) joined += (joined.empty() ? "" : " ") + c;
    log("{} → {} device(s) from {}", joined, state.shown.devices.size(), picture.string());
}

void paint_colours(ServiceState& state, DriverModules& drivers, Clock& clock, const std::vector<Rgb>& stops, Mode mode) {
    const Config& c = state.config;
    DriversState& d = state.drivers;
    Painter painter(stops, mode, c.calibration);
    std::vector<Output> outputs;
    std::vector<ipc::DeviceInfo> found;
    std::map<std::string, std::string> failed;  // driver → what went wrong

    for (const auto& [name, settings] : d.loaded) {
        try {
            plan_outputs(name, drivers.devices(name), c.components, painter, outputs, found);
        } catch (const Error& e) {
            failed[name] = e.what();
        }
    }
    fade(d, drivers, clock, outputs, c.led.fade, failed);

    state.shown.devices = std::move(found);  // what answered, also when something failed
    for (const auto& [name, settings] : d.loaded) {
        if (!failed.contains(name)) d.errors.erase(name);
    }
    if (failed.empty()) {
        state.shown.error.reset();
        state.shown.last_sent = clock.unix_seconds();
        state.retry.reset();
        return;
    }
    std::string message;
    for (const auto& [name, error] : failed) {
        d.errors[name] = error;
        message += std::format("{}{}: {}", message.empty() ? "" : "; ", name, error);
    }
    schedule_retry(state, clock, message);
}

void schedule_retry(ServiceState& state, Clock& clock, const std::string& problem) {
    RetryDelay delay = next_retry_delay(state.retry ? std::optional(state.retry->delay) : std::nullopt);
    if (state.shown.error != problem) {
        log("{} (retrying, the first time in {}s)", problem, std::chrono::duration_cast<std::chrono::seconds>(delay).count());
    }
    state.shown.error = problem;
    state.retry = ServiceState::Retry{clock.now() + delay, delay};
}

std::optional<TimePoint::duration> time_to_retry(const ServiceState& state, Clock& clock) {
    if (!state.retry) return std::nullopt;
    return std::max(state.retry->at - clock.now(), TimePoint::duration::zero());
}

}  // namespace wpc
