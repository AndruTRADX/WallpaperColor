#include "client/tui.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <format>
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
#include "core/arc.hpp"
#include "core/config.hpp"
#include "core/extract.hpp"
#include "core/ipc.hpp"
#include "openrgb/client.hpp"
#include "util/error.hpp"

namespace wpc {

namespace fs = std::filesystem;
namespace ui = ftxui;

namespace {

/// One line of the settings list.
struct Item {
    enum class Kind { Mode, Intensity, Gain };
    Kind kind;
    size_t entry = 0;  // calibration entry (Gain)
    int channel = 0;   // 0 red, 1 green, 2 blue (Gain)
};

ui::Color to_color(Rgb c) {
    return ui::Color::RGB(c.r, c.g, c.b);
}

/// "███████░░░░" showing 0-1 over 20 cells.
std::string bar(double v) {
    int filled = static_cast<int>(std::round(std::clamp(v, 0.0, 1.0) * 20.0));
    std::string out;
    for (int i = 0; i < 20; ++i) out += (i < filled) ? "█" : "░";
    return out;
}

/// A key name in the help line.
ui::Element key(const std::string& k) {
    return ui::text(k) | ui::bold | ui::color(ui::Color::Cyan);
}

/// A bordered box with a title, `rows` lines tall inside.
ui::Element panel(const std::string& title, ui::Elements lines, int rows) {
    return ui::window(ui::text(title), ui::vbox(std::move(lines)), ui::LIGHT) |
           ui::size(ui::HEIGHT, ui::EQUAL, rows + 2);
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
    void on_key(const ui::Event& event);
    void adjust(Item item, double direction);
    std::vector<Rgb> stops() const;

    ui::Element draw() const;
    ui::Element draw_header() const;
    ui::Element draw_devices() const;
    ui::Element draw_settings() const;

    Settings settings_;
    Config config_;
    /// The service's status, or why it could not be read.
    std::optional<ipc::Status> status_;
    std::string status_error_;
    /// Picture the local preview was computed from, and its stops.
    std::optional<fs::path> preview_image_;
    std::vector<Rgb> preview_stops_;
    size_t selected_ = 0;
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
        preview_stops_ = extract_path(image, config_.extract).stops(config_.led.mode);
    } catch (const Error&) {
        preview_stops_.clear();
    }
    preview_image_ = image;
}

std::vector<Item> App::items() const {
    std::vector<Item> items = {{Item::Kind::Mode}, {Item::Kind::Intensity}};
    for (size_t i = 0; i < config_.calibration.size(); ++i) {
        for (int ch = 0; ch < 3; ++ch) items.push_back({Item::Kind::Gain, i, ch});
    }
    return items;
}

void App::on_key(const ui::Event& e) {
    std::vector<Item> list = items();
    using E = ui::Event;
    if (e == E::Character('q') || e == E::Escape) {
        quit_ = true;
    } else if (e == E::ArrowUp || e == E::Character('k')) {
        if (selected_ > 0) --selected_;
    } else if (e == E::ArrowDown || e == E::Character('j')) {
        selected_ = std::min(selected_ + 1, list.size() - 1);
    } else if (e == E::ArrowLeft || e == E::Character('h')) {
        adjust(list[selected_], -1.0);
    } else if (e == E::ArrowRight || e == E::Character('l')) {
        adjust(list[selected_], 1.0);
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

void App::adjust(Item item, double direction) {
    bool edited = true;
    switch (item.kind) {
        case Item::Kind::Mode:
            edited = settings_.set_mode(config_.led.mode == Mode::Arc ? Mode::Single : Mode::Arc);
            break;
        case Item::Kind::Intensity:
            edited = settings_.set_intensity(config_.extract.min_saturation + 0.05 * direction);
            break;
        case Item::Kind::Gain:
            edited = settings_.set_gain(item.entry, item.channel,
                                        config_.calibration[item.entry].gain[item.channel] + 0.05 * direction);
            break;
    }
    if (!edited) {
        message_ = "this setting has a layout the UI does not edit: change it in the config file";
        return;
    }
    config_ = settings_.config();
    try {
        settings_.save();
        message_ = "saved; the service applies it in about a second";
    } catch (const Error& e) {
        message_ = std::format("cannot save the config: {}", e.what());
    }
    if (preview_image_) {
        fs::path image = *preview_image_;
        recompute(image);
    }
}

std::vector<Rgb> App::stops() const {
    if (preview_image_ && !preview_stops_.empty()) return preview_stops_;
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

ui::Element App::draw() const {
    const std::vector<ipc::DeviceInfo> none;
    const std::vector<ipc::DeviceInfo>& devices = status_ ? status_->devices : none;
    int device_rows = 0;
    for (const ipc::DeviceInfo& d : devices) device_rows += 1 + static_cast<int>(d.zones.size());
    device_rows = std::max(device_rows, 1);
    int setting_rows = static_cast<int>(items().size() + config_.calibration.size());

    ui::Element help = ui::hbox({key("↑↓"), ui::text(" select  "), key("←→"), ui::text(" change  "),
                                 key("p"), ui::text(" pause/resume  "), key("a"), ui::text(" apply  "),
                                 key("q"), ui::text(" quit")});
    return ui::vbox({
        draw_header() | ui::size(ui::HEIGHT, ui::EQUAL, 6),
        draw_devices(),
        panel(" Settings ", {draw_settings()}, setting_rows),
        ui::vbox({ui::text(message_) | ui::italic, help}) | ui::flex,
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
    return ui::window(ui::text(" WallpaperColor "), ui::vbox(std::move(lines)), ui::LIGHT);
}

ui::Element App::draw_devices() const {
    std::vector<Rgb> colours = stops();
    ui::Elements lines;
    const std::vector<ipc::DeviceInfo> none;
    const std::vector<ipc::DeviceInfo>& devices = status_ ? status_->devices : none;
    if (devices.empty()) {
        lines.push_back(ui::text("no devices yet: in OpenLinkHub, turn on \"OpenRGB Integration\"") | ui::dim);
    }
    for (const ipc::DeviceInfo& d : devices) {
        lines.push_back(ui::text(d.name) | ui::bold);
        for (const ipc::ZoneInfo& z : d.zones) {
            std::array<float, 3> gain = config_.gain(d.name, z.name);
            ui::Elements row = {ui::text(std::format("  {:<26.26} {:>3} ", z.name, z.leds))};
            for (Rgb c : gradient(colours, z.leds, d.kind != openrgb::DEVICE_TYPE_DRAM)) {
                row.push_back(ui::text("██") | ui::color(to_color(openrgb::scale(c, gain))));
            }
            if (gain != std::array<float, 3>{1, 1, 1}) row.push_back(ui::text(" calibrated") | ui::dim);
            lines.push_back(ui::hbox(std::move(row)));
        }
    }
    int rows = std::max(static_cast<int>(lines.size()), 1);
    return panel(" Devices ", std::move(lines), rows);
}

ui::Element App::draw_settings() const {
    ui::Elements lines;
    std::vector<Item> list = items();
    for (size_t n = 0; n < list.size(); ++n) {
        const Item& item = list[n];
        if (item.kind == Item::Kind::Gain && item.channel == 0) {
            const Calibration& c = config_.calibration[item.entry];
            std::string zone = c.zone ? " · " + *c.zone : "";
            lines.push_back(ui::text(std::format("Calibration: {}{}", c.device, zone)) | ui::dim);
        }
        std::string label;
        std::string value;
        switch (item.kind) {
            case Item::Kind::Mode:
                label = "Mode";
                value = config_.led.mode == Mode::Arc ? "‹ arc ›  three colours as a gradient"
                                                      : "‹ single ›  one colour everywhere";
                break;
            case Item::Kind::Intensity:
                label = "Intensity";
                value = std::format("{} {:.2f}", bar(config_.extract.min_saturation), config_.extract.min_saturation);
                break;
            case Item::Kind::Gain: {
                const char* names[] = {"Red", "Green", "Blue"};
                double v = config_.calibration[item.entry].gain[item.channel];
                label = std::string("  ") + names[item.channel];
                value = std::format("{} {:.2f}", bar(v), v);
                break;
            }
        }
        ui::Element name = ui::text(std::format(" {:<12} ", label));
        if (n == selected_) name = name | ui::inverted;
        lines.push_back(ui::hbox({name, ui::text(" " + value)}));
    }
    return ui::vbox(std::move(lines));
}

}  // namespace

void run_tui(const fs::path& config_path) {
    App app(config_path);
    app.run();
}

}  // namespace wpc
