#include "terminal/presentation/tui/widgets.hpp"

#include <algorithm>
#include <cmath>

namespace wpc::tui {

ui::Color to_color(Rgb c) {
    return ui::Color::RGB(c.r, c.g, c.b);
}

ui::Element bar(double v, double min, double max, ui::Color colour) {
    double t = (max > min) ? (v - min) / (max - min) : 0.0;
    int filled = static_cast<int>(std::round(std::clamp(t, 0.0, 1.0) * 20.0));
    std::string done;
    std::string rest;
    for (int i = 0; i < 20; ++i) (i < filled ? done : rest) += (i < filled) ? "━" : "─";
    return ui::hbox({ui::text(done) | ui::color(colour), ui::text(rest) | ui::dim});
}

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

}  // namespace wpc::tui
