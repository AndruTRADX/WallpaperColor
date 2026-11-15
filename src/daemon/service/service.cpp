#include "daemon/service/service.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <span>

#include "colours/colour.hpp"
#include "colours/extract.hpp"
#include "colours/paint.hpp"
#include "protocol/json.hpp"
#include "utils/error.hpp"
#include "utils/log.hpp"
#include "utils/memory.hpp"

namespace wpc {

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------------------------
// Settings

const DriverModule* find_module(const std::vector<DriverModule>& installed, const std::string& name) {
    auto it = std::find_if(installed.begin(), installed.end(), [&](const DriverModule& m) { return m.name == name; });
    return it == installed.end() ? nullptr : &*it;
}

/// Says what in the settings no installed driver uses.
void log_unused(const std::vector<DriverModule>& installed, const Config& config) {
    for (const auto& [table, settings] : config.drivers.tables) {
        bool known = std::any_of(installed.begin(), installed.end(), [&](const DriverModule& m) { return m.config_table == table; });
        if (!known) log("config: no installed driver uses [{}]", table);
    }
    if (!config.components.chosen) return;
    std::vector<std::vector<Component>> paints;
    for (const DriverModule& m : installed) paints.push_back(m.components);
    std::string orphans;
    for (Component c : unpainted(paints, config.components)) orphans += (orphans.empty() ? "" : ", ") + std::string(component_name(c));
    if (!orphans.empty()) log("no installed driver paints {}", orphans);
}

// ---------------------------------------------------------------------------------------------
// Painting

/// What decides the colours: the picture, its mtime, the mode and the colour settings.
std::string key_of(const fs::path& picture, std::optional<int64_t> mtime, const Config& config) {
    std::string mtime_text = mtime ? std::to_string(*mtime) : "none";
    const ExtractConfig& x = config.extract;
    return std::format("{} {} {} {} {} {} {} {} {} {}", picture.string(), mtime_text, mode_name(config.led.mode),
                       x.palette_size, x.sample_size, x.min_saturation, x.min_chroma, x.min_share, x.skin_weight,
                       x.pastel_gain);
}

/// One output during a fade: where it starts, and its frame with the zones over it. Made once
/// per paint; every frame is written over the same buffer.
struct Fading {
    std::vector<Rgb> from;
    std::vector<Rgb> frame;
    std::vector<ZoneFrame> zones;  // spans over `frame`
};

/// Shows the fade's frames on every output, FADE_FPS a second; a driver that fails is left out
/// of the next frames, its problem in `failed`. Every output gets the first frame; after it, an
/// output whose frame is the same as the one it got last is not sent again (it shows it).
void fade(DriversState& d, DriverHost& drivers, Clock& clock, const std::vector<Output>& outputs, double seconds,
          std::map<std::string, std::string>& failed) {
    std::vector<Fading> fading(outputs.size());
    bool changes = false;
    for (size_t i = 0; i < outputs.size(); ++i) {
        const Output& o = outputs[i];
        Fading& out = fading[i];
        auto last = d.shown.find(o.key);
        out.from = fade_start(last != d.shown.end() ? &last->second : nullptr, o.reported, o.target);
        changes = changes || out.from != o.target;
        out.frame.resize(o.target.size());
        size_t at = 0;
        for (auto [zone, count] : o.zones) {
            out.zones.push_back(ZoneFrame{zone, std::span<const Rgb>(out.frame).subspan(at, count)});
            at += count;
        }
    }

    int frames = fade_frames(seconds, changes);
    TimePoint start = clock.now();
    for (int f = 1; f <= frames; ++f) {
        for (size_t i = 0; i < outputs.size(); ++i) {
            const Output& o = outputs[i];
            if (failed.contains(o.driver)) continue;
            Fading& out = fading[i];
            bool changed = fade_frame_into(out.from, o.target, f, frames, out.frame);
            if (f > 1 && !changed) continue;
            try {
                drivers.show(o.driver, o.device, out.zones);
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

// ---------------------------------------------------------------------------------------------
// Settings

void apply_settings(ServiceState& state, SettingsSource& settings, DriverHost& drivers, bool first) {
    try {
        state.config = settings.load();
    } catch (const Error& e) {
        log("{}; using the defaults", e.what());
        state.config = Config{};
    }

    DriversState& d = state.drivers;
    const ComponentSettings& components = state.config.components;
    std::vector<std::string> problems;
    d.installed = drivers.installed(problems);
    for (const std::string& p : problems) log("{}", p);
    if (d.installed.empty()) log("no driver modules found (looked in {})", drivers.location());
    log_unused(d.installed, state.config);

    // Unload what isn't needed, or whose settings changed (it is loaded again below).
    bool unloaded = false;
    for (auto it = d.loaded.begin(); it != d.loaded.end();) {
        const DriverModule* m = find_module(d.installed, it->first);
        bool keep = m && driver_needed(m->components, components) && state.config.drivers.of(m->config_table) == it->second;
        if (keep) {
            ++it;
            continue;
        }
        drivers.unload(it->first);
        log("driver {}: unloaded", it->first);
        d.errors.erase(it->first);
        it = d.loaded.erase(it);
        unloaded = true;
    }
    if (unloaded) release_free_memory();  // give the driver's memory back to the system

    for (const DriverModule& m : d.installed) {
        if (!driver_needed(m.components, components) || d.loaded.contains(m.name)) continue;
        DriverSettings table = state.config.drivers.of(m.config_table);
        try {
            for (const std::string& key : drivers.load(m.name, table)) {
                log("config: [{}] {}: unknown setting", m.config_table, key);
            }
            d.loaded.emplace(m.name, table);
            std::string chosen;
            for (Component c : m.components) {
                if (components.wants(c)) chosen += (chosen.empty() ? "" : ", ") + std::string(component_name(c));
            }
            log("driver {}: loaded for {}", m.name, chosen);
        } catch (const Error& e) {
            d.errors[m.name] = e.what();
            log("driver {}: {}", m.name, e.what());
        }
    }
    d.shown.clear();  // devices may have changed: fades start from what they report

    if (first) {
        log("wallpapercolord {}: mode {}", WPC_VERSION, mode_name(state.config.led.mode));
    } else {
        log("config reloaded: mode {}", mode_name(state.config.led.mode));
    }
}

// ---------------------------------------------------------------------------------------------
// Painting

void paint_wallpaper(ServiceState& state, Ports& ports, bool force, const std::function<void(const fs::path&)>& reading) {
    OnScreen on;
    try {
        on = ports.desktop.on_screen();
    } catch (const Error& e) {
        return schedule_retry(state, ports.clock, std::string("cannot ask the desktop for the wallpaper: ") + e.what());
    }
    if (on.plugin.empty()) {
        // No desktop for now (the monitor went off and Plasma has no screen): no signal says when
        // it is back, so ask again. The slideshow folders stay watched.
        return schedule_retry(state, ports.clock, "the desktop has no screen for now");
    }
    state.shown.plugin = on.plugin;
    state.slide_folders = on.slide_folders;
    if (!on.picture) {
        state.retry.reset();
        return log("no picture to take colours from (plugin {})", on.plugin);
    }

    const fs::path& picture = *on.picture;
    state.shown.image = picture;
    if (reading) reading(picture);
    std::string key = key_of(picture, ports.pictures.modified_time(picture), state.config);
    if (!force && !state.retry && state.last_painted == key) return;

    if (state.extracted_key != key) {
        try {
            Picture read = ports.pictures.read(picture, picture_side(state.config.extract));
            state.extracted_stops = extract(read, state.config.extract).stops(state.config.led.mode);
            release_free_memory();  // give the decoded picture's memory back to the system
        } catch (const Error& e) {
            release_free_memory();
            state.retry.reset();
            state.shown.error = std::format("{}: {}", picture.string(), e.what());
            return log("{}: {}", picture.string(), e.what());
        }
        state.extracted_key = key;
    }
    state.shown.stops.clear();
    for (Rgb c : state.extracted_stops) state.shown.stops.push_back(to_hex(c));
    if (state.paused) {
        state.last_painted.reset();  // paint on resume
        state.retry.reset();         // nothing to try again until then (a due retry would spin)
        return;
    }

    paint_colours(state, ports.drivers, ports.clock, state.extracted_stops, state.config.led.mode);
    if (state.retry) return;
    state.last_painted = key;
    std::string joined;
    for (const std::string& c : state.shown.stops) joined += (joined.empty() ? "" : " ") + c;
    log("{} → {} device(s) from {}", joined, state.shown.devices.size(), picture.string());
}

void paint_colours(ServiceState& state, DriverHost& drivers, Clock& clock, const std::vector<Rgb>& stops, Mode mode) {
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

void hardware_woke(ServiceState& state) {
    state.drivers.shown.clear();
    log("woke up: painting again");
}

void start_white_test(ServiceState& state) {
    state.testing = true;
    log("white test on");
}

void stop_white_test(ServiceState& state) {
    state.testing = false;
    log("white test off");
}

void paint_white(ServiceState& state, DriverHost& drivers, Clock& clock) {
    state.last_painted.reset();  // the wallpaper's colours are painted again when the test ends
    paint_colours(state, drivers, clock, {WHITE}, Mode::Single);
}

void pause(ServiceState& state) {
    state.paused = true;
    log("paused");
}

void resume(ServiceState& state) {
    state.paused = false;
    log("resumed");
}

// ---------------------------------------------------------------------------------------------
// Retries

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

// ---------------------------------------------------------------------------------------------
// Requests

ipc::Status report_status(const ServiceState& state) {
    ipc::Status s = state.shown;
    s.version = WPC_VERSION;
    s.paused = state.paused;
    s.testing = state.testing;
    s.mode = state.config.led.mode;
    s.drivers.clear();
    for (const DriverModule& m : state.drivers.installed) {
        ipc::DriverState d{m.name, m.components, state.drivers.loaded.contains(m.name), {}};
        if (auto e = state.drivers.errors.find(m.name); e != state.drivers.errors.end()) d.error = e->second;
        s.drivers.push_back(std::move(d));
    }
    return s;
}

ipc::Response run_request(ipc::Request request, ServiceState& state, bool& paint_now) {
    switch (request) {
        case ipc::Request::Status:
            return ipc::Response::of(report_status(state));
        case ipc::Request::Pause:
            pause(state);
            return ipc::Response::ok();
        case ipc::Request::Resume:
            resume(state);
            break;
        case ipc::Request::Apply:
            break;
        case ipc::Request::Test:
            start_white_test(state);
            break;
        case ipc::Request::TestOff:
            stop_white_test(state);
            break;
    }
    paint_now = true;
    return ipc::Response::ok();
}

std::string answer_line(const std::string& line, ServiceState& state, bool& paint_now) {
    ipc::Response answer;
    try {
        answer = run_request(ipc::decode_request(line), state, paint_now);
    } catch (const Error& e) {
        answer = ipc::Response::error(std::string("bad request: ") + e.what());
    }
    return ipc::encode(answer);
}

}  // namespace wpc
