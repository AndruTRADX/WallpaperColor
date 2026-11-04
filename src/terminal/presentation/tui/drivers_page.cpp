// The Drivers page: which components are painted, and the drivers' own settings. The service
// keeps a driver in memory only while one of its components is ticked here.

#include <algorithm>
#include <format>

#include "terminal/application/features/choose_components/choose_components.hpp"
#include "terminal/application/features/driver_options/driver_options.hpp"
#include "terminal/presentation/tui/app.hpp"
#include "terminal/presentation/tui/widgets.hpp"

namespace wpc::tui {

void App::open_drivers_page() {
    drivers_page_ = true;
    detection_ = detect_hardware(parts_.drivers, config_);
}

std::vector<DriverOptionValue> App::options() const {
    return detection_ ? driver_options(detection_->modules, config_) : std::vector<DriverOptionValue>{};
}

void App::on_drivers_key(const ui::Event& e) {
    using E = ui::Event;
    size_t components = ALL_COMPONENTS.size();
    std::vector<DriverOptionValue> settings = options();
    size_t count = components + settings.size();  // the components' lines, then the settings'
    driver_line_ = std::min(driver_line_, count - 1);
    bool change = e == E::Character(' ') || e == E::Return || e == E::ArrowLeft || e == E::ArrowRight ||
                  e == E::Character('h') || e == E::Character('l');
    if (e == E::Escape || e == E::Character('d') || e == E::Character('q')) {
        drivers_page_ = false;
        detection_.reset();  // nothing of the detection stays in memory
    } else if (e == E::ArrowUp || e == E::Character('k')) {
        if (driver_line_ > 0) --driver_line_;
    } else if (e == E::ArrowDown || e == E::Character('j')) {
        driver_line_ = std::min(driver_line_ + 1, count - 1);
    } else if (change && driver_line_ < components) {
        saved(toggle_component(editor_, parts_.format, ALL_COMPONENTS[driver_line_]),
              "saved; the service loads and unloads drivers to match");
    } else if (change) {
        int direction = (e == E::ArrowLeft || e == E::Character('h')) ? -1 : 1;
        saved(cycle_driver_option(editor_, settings[driver_line_ - components], direction),
              "saved");
    } else if (e == E::Character('r')) {
        detection_ = detect_hardware(parts_.drivers, config_);
        message_ = "looked again";
    }
}

ui::Element App::draw_drivers() const {
    std::vector<DriverModule> modules = detection_ ? detection_->modules : std::vector<DriverModule>{};
    std::vector<ComponentChoice> choices = component_choices(modules, config_, status_ ? &*status_ : nullptr);
    std::vector<DriverOptionValue> settings = options();

    // The drivers' column, as wide as its longest entry ("openrgb, razer").
    auto joined = [](const std::vector<std::string>& names) {
        std::string out;
        for (const std::string& n : names) out += (out.empty() ? "" : ", ") + n;
        return out;
    };
    size_t width = 9;
    for (const ComponentChoice& c : choices) width = std::max(width, joined(c.drivers).size());

    ui::Elements rows;
    for (size_t i = 0; i < choices.size(); ++i) {
        const ComponentChoice& choice = choices[i];

        // What the service does with the drivers that paint it.
        ui::Element state = choice.error  ? ui::text("⚠ " + *choice.error) | ui::color(ui::Color::Red)
                            : choice.loaded ? ui::text("● loaded") | ui::color(ui::Color::Green)
                                            : ui::text("");
        std::string drivers = joined(choice.drivers);
        std::string found;
        if (detection_) {
            for (const std::string& f : detection_->found[i]) found += (found.empty() ? "" : ", ") + f;
        }
        if (choice.drivers.empty()) found = "no driver installed for it";
        else if (found.empty()) found = "nothing found";

        ui::Element name = ui::text(std::format(" [{}] {:<13} ", choice.chosen ? "x" : " ", component_label(choice.component)));
        if (i == driver_line_) name = name | ui::inverted;
        rows.push_back(ui::hbox({name, ui::text(std::format(" {:<{}} ", drivers, width)) | ui::dim,
                                 ui::text(std::format("{:<44.44} ", found)), state}));
    }

    ui::Elements setting_rows;
    for (size_t i = 0; i < settings.size(); ++i) {
        const DriverOptionValue& s = settings[i];
        bool selected = choices.size() + i == driver_line_;
        ui::Element name = ui::text(std::format("     {:<13} ", s.option.label));
        if (selected) name = name | ui::inverted;
        ui::Elements row = {name, ui::text(std::format(" {:<{}} ", s.driver, width)) | ui::dim,
                            ui::text(std::format("‹ {} ›", s.value))};
        if (selected) row.push_back(ui::text("   " + s.option.hint) | ui::dim);
        setting_rows.push_back(ui::hbox(std::move(row)));
    }

    ui::Elements problems;
    if (detection_) {
        for (const std::string& p : detection_->problems) problems.push_back(ui::text("⚠ " + p) | ui::color(ui::Color::Red));
    }
    ui::Element help = ui::hbox({key("↑↓"), ui::text(" select  "), key("space"), ui::text(" on/off  "),
                                 key("←→"), ui::text(" change  "),
                                 key("r"), ui::text(" look again  "), key("esc"), ui::text(" back")});
    ui::Element intro = ui::paragraph(
        "Tick what WallpaperColor paints. The service keeps in memory only the drivers of what is ticked.") | ui::dim;
    ui::Elements body = {intro, ui::separator(), ui::vbox(std::move(rows))};
    if (!setting_rows.empty()) {
        body.push_back(ui::separator());
        body.push_back(ui::vbox(std::move(setting_rows)));
    }
    body.push_back(ui::vbox(std::move(problems)));
    return ui::vbox({
        panel(" Drivers ", ui::vbox(std::move(body))),
        ui::filler(),
        ui::hbox({help, ui::filler(), ui::text(message_) | ui::italic}),
    });
}

}  // namespace wpc::tui
