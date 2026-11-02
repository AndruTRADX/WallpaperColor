#include "client/tui.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "client/settings.hpp"
#include "core/config.hpp"
#include "core/extract.hpp"
#include "core/ipc.hpp"
#include "core/paint.hpp"
#include "openrgb/client.hpp"
#include "util/error.hpp"

namespace wpc {

namespace fs = std::filesystem;
namespace ui = ftxui;

namespace {

/// A setting that is a number, edited with ←→ in steps.
struct NumberSetting {
    const char* label;
    const char* table;
    const char* key;
    double step;
    double min;
    double max;
    int decimals;  // -1: a whole number
    const char* unit;
    double (*get)(const Config&);
    const char* hint;
};

const NumberSetting NUMBERS[] = {
    {"Fade", "led", "fade", 0.1, 0.0, 1.5, 1, " s", [](const Config& c) { return c.led.fade; },
     "how long a colour change takes; 0 = at once"},
    {"Intensity", "extract", "min_saturation", 0.05, 0.2, 1.0, 2, "",
     [](const Config& c) { return c.extract.min_saturation; }, "how saturated dark and dull colours become"},
    {"Pastel gain", "extract", "pastel_gain", 0.25, 1.0, 5.0, 2, "",
     [](const Config& c) { return c.extract.pastel_gain; }, "how much light, soft colours are strengthened"},
    {"Skin weight", "extract", "skin_weight", 0.05, 0.0, 1.0, 2, "",
     [](const Config& c) { return c.extract.skin_weight; }, "how much skin tones count (0 = never picked)"},
    {"Min chroma", "extract", "min_chroma", 0.01, 0.0, 0.5, 2, "",
     [](const Config& c) { return c.extract.min_chroma; }, "colours duller than this count as grey"},
    {"Min share", "extract", "min_share", 0.001, 0.0, 0.05, 3, "",
     [](const Config& c) { return c.extract.min_share; }, "colours covering less of the picture are ignored"},
    {"Palette size", "extract", "palette_size", 1, 2, 32, -1, "",
     [](const Config& c) { return static_cast<double>(c.extract.palette_size); },
     "how many colours the picture is reduced to"},
    {"Sample size", "extract", "sample_size", 16, 16, 512, -1, " px",
     [](const Config& c) { return static_cast<double>(c.extract.sample_size); },
     "the picture is shrunk to this size first"},
};

/// One line of the settings list.
struct Item {
    enum class Kind { Mode, Motherboard, Number, Gain, AddCalibration };
    Kind kind;
    size_t index = 0;  // NUMBERS entry (Number) or calibration entry (Gain)
    int channel = 0;   // 0 red, 1 green, 2 blue (Gain)
};

/// A device, or one zone of it, that a new calibration entry can be for.
struct CalibrationTarget {
    std::string device;
    std::optional<std::string> zone;
};

ui::Color to_color(Rgb c) {
    return ui::Color::RGB(c.r, c.g, c.b);
}

/// A thin bar "━━━━━━────" showing where `v` is between `min` and `max`, over 20 cells:
/// the filled part in `colour`, the rest dimmed.
ui::Element bar(double v, double min, double max, ui::Color colour) {
    double t = (max > min) ? (v - min) / (max - min) : 0.0;
    int filled = static_cast<int>(std::round(std::clamp(t, 0.0, 1.0) * 20.0));
    std::string done;
    std::string rest;
    for (int i = 0; i < 20; ++i) (i < filled ? done : rest) += (i < filled) ? "━" : "─";
    return ui::hbox({ui::text(done) | ui::color(colour), ui::text(rest) | ui::dim});
}

/// A key name in the help line.
ui::Element key(const std::string& k) {
    return ui::text(k) | ui::bold | ui::color(ui::Color::Cyan);
}

ui::Element panel(const std::string& title, ui::Element content) {
    return ui::window(ui::text(title), std::move(content), ui::LIGHT);
}

std::string_view mode_description(Mode mode) {
    switch (mode) {
        case Mode::Arc: return "‹ arc ›  three colours as a gradient";
        case Mode::Single: return "‹ single ›  one colour everywhere";
        case Mode::Split: return "‹ split ›  one colour per kind of zone (RAM, pump, fans)";
    }
    return "";
}

class App {
public:
    explicit App(const fs::path& config_path)
        : settings_(Settings::load(config_path)), config_(settings_.config()) {}

    void run();

private:
    void refresh();
    void recompute(const fs::path& image);
    std::vector<Item> items() const;
    std::vector<CalibrationTarget> calibration_targets() const;
    void on_key(const ui::Event& event);
    void adjust(const Item& item, int direction);
    void add_calibration();
    void remove_calibration(size_t entry);
    void saved(bool edited, const std::string& done);
    std::vector<Rgb> stops() const;
    const std::vector<ipc::DeviceInfo>& devices() const;

    ui::Element draw();
    ui::Element draw_header() const;
    ui::Element draw_devices() const;
    ui::Element draw_settings();
    ui::Element draw_item(const Item& item, bool selected);

    Settings settings_;
    Config config_;
    /// The service's status, or why it could not be read.
    std::optional<ipc::Status> status_;
    std::string status_error_;
    /// Picture the local preview was computed from, and its analysis.
    std::optional<fs::path> preview_image_;
    std::optional<Extraction> preview_;
    size_t selected_ = 0;
    /// Which device or zone "Add calibration" points at.
    size_t add_choice_ = 0;
    /// Calibration entry waiting for a second `x` to be removed.
    std::optional<size_t> pending_remove_;
    std::string message_;
    bool quit_ = false;
};

void App::run() {
    ui::ScreenInteractive screen = ui::ScreenInteractive::Fullscreen();

    // Every second, ask the event loop to refresh (this thread only exists while the UI is open).
    std::mutex mutex;
    std::condition_variable_any tick;
    std::jthread ticker([&](std::stop_token stop) {
        std::unique_lock lock(mutex);
        while (!tick.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; })) {
            if (stop.stop_requested()) return;
            screen.PostEvent(ui::Event::Custom);
        }
    });

    refresh();
    ui::Component renderer = ui::Renderer([&] { return draw(); });
    ui::Component component = ui::CatchEvent(renderer, [&](const ui::Event& event) {
        if (event == ui::Event::Custom) {
            refresh();
            return true;
        }
        on_key(event);
        refresh();  // show the effect of the key at once
        if (quit_) screen.Exit();
        return true;
    });
    screen.Loop(component);
}

void App::refresh() {
    try {
        ipc::Response r = ipc::call(ipc::Request::Status);
        if (r.kind == ipc::Response::Kind::Status) {
            status_ = r.status;
        } else {
            status_.reset();
            status_error_ = r.kind == ipc::Response::Kind::Error ? r.message : "the service gave no status";
        }
    } catch (const Error& e) {
        status_.reset();
        status_error_ = std::format("service not running ({}): systemctl --user start wallpapercolord", e.what());
    }
    if (status_ && status_->image && preview_image_ != status_->image) recompute(*status_->image);
}

/// Local preview with the settings being edited.
void App::recompute(const fs::path& image) {
    try {
        preview_ = extract_path(image, config_.extract);
    } catch (const Error&) {
        preview_.reset();
    }
    preview_image_ = image;
}

const std::vector<ipc::DeviceInfo>& App::devices() const {
    static const std::vector<ipc::DeviceInfo> none;
    return status_ ? status_->devices : none;
}

std::vector<Item> App::items() const {
    std::vector<Item> items = {{Item::Kind::Mode}, {Item::Kind::Motherboard}};
    for (size_t i = 0; i < std::size(NUMBERS); ++i) items.push_back({Item::Kind::Number, i});
    for (size_t i = 0; i < config_.calibration.size(); ++i) {
        for (int ch = 0; ch < 3; ++ch) items.push_back({Item::Kind::Gain, i, ch});
    }
    items.push_back({Item::Kind::AddCalibration});
    return items;
}

std::vector<CalibrationTarget> App::calibration_targets() const {
    std::vector<CalibrationTarget> targets;
    std::vector<std::string> seen;
    for (const ipc::DeviceInfo& d : devices()) {
        if (std::find(seen.begin(), seen.end(), d.name) != seen.end()) continue;  // e.g. the second RAM stick
        seen.push_back(d.name);
        targets.push_back({d.name, std::nullopt});
        std::vector<std::string> zones;
        for (const ipc::ZoneInfo& z : d.zones) {
            if (std::find(zones.begin(), zones.end(), z.name) != zones.end()) continue;
            zones.push_back(z.name);
            targets.push_back({d.name, z.name});
        }
    }
    return targets;
}

void App::on_key(const ui::Event& e) {
    std::vector<Item> list = items();
    selected_ = std::min(selected_, list.size() - 1);
    const Item& item = list[selected_];
    using E = ui::Event;
    if (e != E::Character('x')) pending_remove_.reset();

    if (e == E::Character('q') || e == E::Escape) {
        quit_ = true;
    } else if (e == E::ArrowUp || e == E::Character('k')) {
        if (selected_ > 0) --selected_;
    } else if (e == E::ArrowDown || e == E::Character('j')) {
        selected_ = std::min(selected_ + 1, list.size() - 1);
    } else if (e == E::ArrowLeft || e == E::Character('h')) {
        adjust(item, -1);
    } else if (e == E::ArrowRight || e == E::Character('l')) {
        adjust(item, 1);
    } else if (e == E::Return && item.kind == Item::Kind::AddCalibration) {
        add_calibration();
    } else if (e == E::Character('x') && item.kind == Item::Kind::Gain) {
        remove_calibration(item.index);
    } else if (e == E::Character('p')) {
        bool paused = status_ && status_->paused;
        try {
            ipc::call(paused ? ipc::Request::Resume : ipc::Request::Pause);
            message_ = paused ? "resumed" : "paused: colours are not sent until you resume";
        } catch (const Error& err) {
            message_ = std::format("service not reachable: {}", err.what());
        }
    } else if (e == E::Character('a')) {
        try {
            ipc::call(ipc::Request::Apply);
            message_ = "colours sent again";
        } catch (const Error& err) {
            message_ = std::format("service not reachable: {}", err.what());
        }
    }
}

void App::adjust(const Item& item, int direction) {
    switch (item.kind) {
        case Item::Kind::Mode: {
            // arc → single → split → arc (or the other way round)
            int next = (static_cast<int>(config_.led.mode) + direction + 3) % 3;
            saved(settings_.set_mode(static_cast<Mode>(next)), "saved");
            break;
        }
        case Item::Kind::Motherboard:
            saved(settings_.set_motherboard(!config_.motherboard.enabled), "saved");
            break;
        case Item::Kind::Number: {
            const NumberSetting& n = NUMBERS[item.index];
            double v = std::clamp(n.get(config_) + n.step * direction, n.min, n.max);
            bool edited = (n.decimals < 0) ? settings_.set_integer(n.table, n.key, std::lround(v))
                                           : settings_.set_float(n.table, n.key, v, n.decimals);
            saved(edited, "saved");
            break;
        }
        case Item::Kind::Gain: {
            double v = config_.calibration[item.index].gain[item.channel] + 0.05 * direction;
            saved(settings_.set_gain(item.index, item.channel, v), "saved");
            break;
        }
        case Item::Kind::AddCalibration: {
            size_t count = calibration_targets().size();
            if (count > 0) add_choice_ = (add_choice_ + count + direction) % count;
            break;
        }
    }
}

void App::add_calibration() {
    std::vector<CalibrationTarget> targets = calibration_targets();
    if (targets.empty()) {
        message_ = "no devices to calibrate yet";
        return;
    }
    const CalibrationTarget& t = targets[std::min(add_choice_, targets.size() - 1)];
    std::optional<std::string_view> zone;
    if (t.zone) zone = *t.zone;
    saved(settings_.add_calibration(t.device, zone), "calibration added: lower the channels that look too strong");
    selected_ = items().size() - 4;  // the new entry's Red line
}

void App::remove_calibration(size_t entry) {
    const Calibration& c = config_.calibration[entry];
    std::string name = c.device + (c.zone ? " · " + *c.zone : "");
    if (pending_remove_ != entry) {
        pending_remove_ = entry;
        message_ = "press x again to remove the calibration for " + name;
        return;
    }
    pending_remove_.reset();
    saved(settings_.remove_calibration(entry), "removed the calibration for " + name);
    selected_ = std::min(selected_, items().size() - 1);
}

/// After an edit: reload, save and update the preview.
void App::saved(bool edited, const std::string& done) {
    if (!edited) {
        message_ = "this setting has a layout the UI does not edit: change it in the config file";
        return;
    }
    config_ = settings_.config();
    try {
        settings_.save();
        message_ = done + "; the service applies it in about a second";
    } catch (const Error& e) {
        message_ = std::format("cannot save the config: {}", e.what());
    }
    if (preview_image_) {
        fs::path image = *preview_image_;
        recompute(image);
    }
}

std::vector<Rgb> App::stops() const {
    if (preview_) return preview_->stops(config_.led.mode);
    std::vector<Rgb> out;
    if (status_) {
        for (const std::string& hex : status_->stops) {
            if (std::optional<Rgb> c = Rgb::parse(hex)) out.push_back(*c);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Drawing

ui::Element App::draw() {
    ui::Element help = ui::hbox({key("↑↓"), ui::text(" select  "), key("←→"), ui::text(" change  "),
                                 key("enter"), ui::text(" add  "), key("x"), ui::text(" remove  "),
                                 key("p"), ui::text(" pause/resume  "), key("a"), ui::text(" apply  "),
                                 key("q"), ui::text(" quit")});
    return ui::vbox({
        draw_header(),
        draw_devices(),
        panel(" Settings ", draw_settings()) | ui::flex_shrink,  // shrinks (and scrolls) only if needed
        ui::filler(),
        ui::hbox({help, ui::filler(), ui::text(message_) | ui::italic}),
    });
}

ui::Element App::draw_header() const {
    ui::Elements lines;
    if (status_) {
        const ipc::Status& s = *status_;
        ui::Element state = s.paused ? ui::text("⏸ paused") | ui::bold | ui::color(ui::Color::Yellow)
                                     : ui::text("● running") | ui::bold | ui::color(ui::Color::Green);
        lines.push_back(ui::hbox({state, ui::text(std::format("  v{}  ·  {}", s.version, s.plugin)) | ui::dim}));
        std::string name = s.image ? s.image->filename().string() : "—";
        lines.push_back(ui::hbox({ui::text("Wallpaper  ") | ui::bold, ui::text(name)}));
        if (s.error) lines.push_back(ui::text("⚠ " + *s.error) | ui::color(ui::Color::Red));
    } else {
        lines.push_back(ui::text("✗ " + status_error_) | ui::bold | ui::color(ui::Color::Red));
    }

    ui::Elements colours = {ui::text("Colours    ") | ui::bold};
    for (Rgb c : stops()) {
        colours.push_back(ui::text("      ") | ui::bgcolor(to_color(c)));
        colours.push_back(ui::text(std::format(" {}  ", c.hex())));
    }
    lines.push_back(ui::hbox(std::move(colours)));

    // The palette the colours were picked from: each entry with its share of the picture.
    if (preview_) {
        ui::Elements palette = {ui::text("Palette    ") | ui::bold};
        for (const PaletteEntry& p : preview_->palette) {
            bool picked = preview_->picked == p.color;
            palette.push_back(ui::text("  ") | ui::bgcolor(to_color(p.color)));
            ui::Element share = ui::text(std::format(" {:.0f}%{} ", p.share * 100.0f, picked ? "←" : " "));
            palette.push_back(picked ? share | ui::bold : share | ui::dim);
        }
        lines.push_back(ui::hbox(std::move(palette)));
    }
    return panel(" WallpaperColor ", ui::vbox(std::move(lines)));
}

ui::Element App::draw_devices() const {
    ui::Elements lines;
    if (devices().empty()) {
        lines.push_back(ui::text("no devices yet: in OpenLinkHub, turn on \"OpenRGB Integration\"") | ui::dim);
    }
    Painter painter(stops(), config_.led.mode, config_);
    std::map<std::string, int> seen;  // devices with the same name are numbered: "VENGEANCE RGB 2"
    for (const ipc::DeviceInfo& d : devices()) {
        long same = std::count_if(devices().begin(), devices().end(),
                                  [&](const ipc::DeviceInfo& other) { return other.name == d.name; });
        std::string title = same > 1 ? std::format("{} {}", d.name, ++seen[d.name]) : d.name;
        bool ring = d.kind != openrgb::DEVICE_TYPE_DRAM;

        // One row of LEDs: the label, the count, the colours, and whether it is calibrated.
        auto leds_row = [&](ui::Element label, const ipc::ZoneInfo& z) {
            ui::Elements row = {label, ui::text(std::format(" {:>3} ", z.leds))};
            for (Rgb c : painter.zone(d.name, z.name, z.leds, ring)) {
                row.push_back(ui::text("██") | ui::color(to_color(c)));
            }
            if (config_.gain(d.name, z.name) != std::array<float, 3>{1, 1, 1}) {
                row.push_back(ui::text(" calibrated") | ui::dim);
            }
            return ui::hbox(std::move(row));
        };
        if (d.zones.size() == 1) {  // a device with one zone (RAM): a single line
            lines.push_back(leds_row(ui::text(std::format("{:<28.28}", title)) | ui::bold, d.zones[0]));
            continue;
        }
        lines.push_back(ui::text(title) | ui::bold);
        for (const ipc::ZoneInfo& z : d.zones) {
            lines.push_back(leds_row(ui::text(std::format("  {:<26.26}", z.name)), z));
        }
    }
    return panel(" Devices ", ui::vbox(std::move(lines)));
}

ui::Element App::draw_settings() {
    ui::Elements lines;
    std::vector<Item> list = items();
    selected_ = std::min(selected_, list.size() - 1);
    for (size_t n = 0; n < list.size(); ++n) {
        const Item& item = list[n];
        if (item.kind == Item::Kind::Gain && item.channel == 0) {
            const Calibration& c = config_.calibration[item.index];
            std::string zone = c.zone ? " · " + *c.zone : " · all zones";
            lines.push_back(ui::text(std::format("Calibration: {}{}", c.device, zone)) | ui::dim);
        }
        ui::Element line = draw_item(item, n == selected_);
        lines.push_back(n == selected_ ? line | ui::focus : line);  // focus: scroll to it
    }
    return ui::vbox(std::move(lines)) | ui::vscroll_indicator | ui::yframe;
}

ui::Element App::draw_item(const Item& item, bool selected) {
    std::string label;
    ui::Element value;
    std::string hint;
    switch (item.kind) {
        case Item::Kind::Mode:
            label = "Mode";
            value = ui::text(std::string(mode_description(config_.led.mode)));
            break;
        case Item::Kind::Motherboard:
            label = "Motherboard";
            value = ui::text(config_.motherboard.enabled ? "‹ on ›" : "‹ off ›");
            hint = "paint the motherboard's own RGB too";
            break;
        case Item::Kind::Number: {
            const NumberSetting& n = NUMBERS[item.index];
            double v = n.get(config_);
            label = n.label;
            std::string number = (n.decimals < 0) ? std::format("{}", std::lround(v))
                                                  : std::format("{:.{}f}", v, n.decimals);
            value = ui::hbox({bar(v, n.min, n.max, ui::Color::Cyan), ui::text(std::format(" {}{}", number, n.unit))});
            hint = n.hint;
            break;
        }
        case Item::Kind::Gain: {
            const char* names[] = {"Red", "Green", "Blue"};
            const ui::Color colours[] = {ui::Color::Red, ui::Color::Green, ui::Color::Blue};
            double v = config_.calibration[item.index].gain[item.channel];
            label = std::string("  ") + names[item.channel];
            value = ui::hbox({bar(v, 0.0, 1.0, colours[item.channel]), ui::text(std::format(" {:.2f}", v))});
            hint = "x twice: remove this calibration";
            break;
        }
        case Item::Kind::AddCalibration: {
            label = "+ Calibrate";
            std::vector<CalibrationTarget> targets = calibration_targets();
            if (targets.empty()) {
                value = ui::text("no devices yet");
            } else {
                const CalibrationTarget& t = targets[std::min(add_choice_, targets.size() - 1)];
                value = ui::text(std::format("‹ {} · {} ›", t.device, t.zone.value_or("all zones")));
                hint = "enter: add";
            }
            break;
        }
    }
    ui::Element name = ui::text(std::format(" {:<12} ", label));
    if (selected) name = name | ui::inverted;
    ui::Elements row = {name, ui::text(" "), value};
    if (selected && !hint.empty()) row.push_back(ui::text("   " + hint) | ui::dim);
    return ui::hbox(std::move(row));
}

}  // namespace

void run_tui(const fs::path& config_path) {
    App app(config_path);
    app.run();
}

}  // namespace wpc
