// The home page: the service's state and colours, a preview of every device, the settings.

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

#include "terminal/application/features/edit_settings/edit_settings.hpp"
#include "terminal/application/features/preview_devices/preview_devices.hpp"
#include "terminal/presentation/tui/app.hpp"
#include "terminal/presentation/tui/widgets.hpp"

namespace wpc::tui {

namespace {

/// How each number setting is shown, in the order of number_settings().
struct NumberLabel {
    const char* label;
    const char* unit;
    const char* hint;
};

const NumberLabel NUMBER_LABELS[] = {
    {"Fade", " s", "how long a colour change takes; 0 = at once"},
    {"Intensity", "", "how saturated dark and dull colours become"},
    {"Pastel gain", "", "how much light, soft colours are strengthened"},
    {"Skin weight", "", "how much skin tones count (0 = never picked)"},
    {"Min chroma", "", "colours duller than this count as grey"},
    {"Min share", "", "colours covering less of the picture are ignored"},
    {"Palette size", "", "how many colours the picture is reduced to"},
    {"Sample size", " px", "the picture is shrunk to this size first"},
};

}  // namespace

void App::on_home_key(const ui::Event& e) {
    std::vector<Item> list = items();
    selected_ = std::min(selected_, list.size() - 1);
    const Item& item = list[selected_];
    using E = ui::Event;
    if (e != E::Character('x')) pending_remove_.reset();

    bool testing = status_ && status_->testing;
    bool paused = status_ && status_->paused;
    if (e == E::Character('q') || e == E::Escape) {
        if (testing) ask(ipc::Request::TestOff, "");  // don't leave the PC white
        quit_ = true;
    } else if (e == E::Character('w')) {
        ask(testing ? ipc::Request::TestOff : ipc::Request::Test,
            testing ? "white test off: back to the wallpaper's colours"
                    : "white test: adjust a calibration until its whites match the others");
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
        ask(paused ? ipc::Request::Resume : ipc::Request::Pause,
            paused ? "resumed" : "paused: colours are not sent until you resume");
    } else if (e == E::Character('a')) {
        ask(ipc::Request::Apply, "colours sent again");
    }
}

std::vector<App::Item> App::items() const {
    std::vector<Item> items = {{Item::Kind::Mode}};
    for (size_t i = 0; i < number_settings().size(); ++i) items.push_back({Item::Kind::Number, i});
    for (size_t i = 0; i < config_.calibration.entries.size(); ++i) {
        for (int ch = 0; ch < 3; ++ch) items.push_back({Item::Kind::Gain, i, ch});
    }
    items.push_back({Item::Kind::AddCalibration});
    return items;
}

void App::adjust(const Item& item, int direction) {
    switch (item.kind) {
        case Item::Kind::Mode:
            return saved(cycle_mode(editor_, parts_.format, direction), "saved");
        case Item::Kind::Number:
            return saved(step_number(editor_, parts_.format, item.index, direction), "saved");
        case Item::Kind::Gain:
            return saved(step_gain(editor_, parts_.format, item.index, item.channel, direction), "saved");
        case Item::Kind::AddCalibration: {
            size_t count = calibration_targets(devices()).size();
            if (count > 0) add_choice_ = (add_choice_ + count + direction) % count;
            return;
        }
    }
}

void App::add_calibration() {
    std::vector<CalibrationTarget> targets = calibration_targets(devices());
    if (targets.empty()) {
        message_ = "no devices to calibrate yet";
        return;
    }
    const CalibrationTarget& t = targets[std::min(add_choice_, targets.size() - 1)];
    saved(wpc::add_calibration(editor_, t), "calibration added: lower the channels that look too strong");
    selected_ = items().size() - 4;  // the new entry's Red line
}

void App::remove_calibration(size_t entry) {
    const Calibration& c = config_.calibration.entries[entry];
    std::string name = c.device + (c.zone ? " · " + *c.zone : "");
    if (pending_remove_ != entry) {
        pending_remove_ = entry;
        message_ = "press x again to remove the calibration for " + name;
        return;
    }
    pending_remove_.reset();
    saved(wpc::remove_calibration(editor_, entry), "removed the calibration for " + name);
    selected_ = std::min(selected_, items().size() - 1);
}

ui::Element App::draw_home() {
    ui::Element help = ui::hbox({key("↑↓"), ui::text(" select  "), key("←→"), ui::text(" change  "),
                                 key("enter"), ui::text(" add  "), key("x"), ui::text(" remove  "),
                                 key("w"), ui::text(" white test  "), key("d"), ui::text(" drivers  "),
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
        if (s.testing) {
            lines.push_back(ui::text("◻ white test: everything is white; compare devices, adjust calibrations, w to end") |
                            ui::bold | ui::color(ui::Color::Yellow));
        }
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
        lines.push_back(ui::text("no devices yet: d shows the drivers and what they find") | ui::dim);
    }
    // What the service paints: the white test, or the wallpaper's colours.
    bool testing = status_ && status_->testing;
    std::vector<PreviewDevice> preview = preview_devices(devices(), stops(), testing, config_);
    std::map<std::string, int> seen;  // devices with the same name are numbered: "VENGEANCE RGB 2"
    for (const PreviewDevice& d : preview) {
        long same = std::count_if(preview.begin(), preview.end(),
                                  [&](const PreviewDevice& other) { return other.name == d.name; });
        std::string title = same > 1 ? std::format("{} {}", d.name, ++seen[d.name]) : d.name;

        // One row of LEDs: the label, the count, the colours, and whether it is calibrated.
        auto leds_row = [&](ui::Element label, const PreviewZone& z) {
            ui::Elements row = {label, ui::text(std::format(" {:>3} ", z.zone.leds))};
            if (!z.chosen) {
                row.push_back(ui::text(std::format("{} off (d: drivers)", component_label(z.zone.component))) | ui::dim);
                return ui::hbox(std::move(row));
            }
            for (Rgb c : z.leds) row.push_back(ui::text("██") | ui::color(to_color(c)));
            if (z.calibrated) row.push_back(ui::text(" calibrated") | ui::dim);
            return ui::hbox(std::move(row));
        };
        if (d.zones.size() == 1) {  // a device with one zone (RAM): a single line
            lines.push_back(leds_row(ui::text(std::format("{:<28.28}", title)) | ui::bold, d.zones[0]));
            continue;
        }
        lines.push_back(ui::text(title) | ui::bold);
        for (const PreviewZone& z : d.zones) {
            lines.push_back(leds_row(ui::text(std::format("  {:<26.26}", z.zone.name)), z));
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
            const Calibration& c = config_.calibration.entries[item.index];
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
        case Item::Kind::Number: {
            const NumberSetting& n = number_settings()[item.index];
            const NumberLabel& shown = NUMBER_LABELS[item.index];
            double v = n.get(config_);
            label = shown.label;
            std::string number = (n.decimals < 0) ? std::format("{}", std::lround(v))
                                                  : std::format("{:.{}f}", v, n.decimals);
            value = ui::hbox({bar(v, n.min, n.max, ui::Color::Cyan), ui::text(std::format(" {}{}", number, shown.unit))});
            hint = shown.hint;
            break;
        }
        case Item::Kind::Gain: {
            const char* names[] = {"Red", "Green", "Blue"};
            const ui::Color colours[] = {ui::Color::Red, ui::Color::Green, ui::Color::Blue};
            double v = config_.calibration.entries[item.index].gain[item.channel];
            label = std::string("  ") + names[item.channel];
            value = ui::hbox({bar(v, 0.0, 1.0, colours[item.channel]), ui::text(std::format(" {:.2f}", v))});
            hint = "x twice: remove this calibration";
            break;
        }
        case Item::Kind::AddCalibration: {
            label = "+ Calibrate";
            std::vector<CalibrationTarget> targets = calibration_targets(devices());
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

}  // namespace wpc::tui
