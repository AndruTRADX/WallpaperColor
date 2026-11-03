// Small drawing pieces the terminal UI's pages share.

#pragma once

#include <string>
#include <string_view>

#include <ftxui/dom/elements.hpp>

#include "shared/domain/colours/color.hpp"
#include "shared/domain/colours/extract.hpp"

namespace wpc::tui {

namespace ui = ftxui;

ui::Color to_color(Rgb c);

/// A thin bar "━━━━━━────" showing where `v` is between `min` and `max`, over 20 cells: the
/// filled part in `colour`, the rest dimmed.
ui::Element bar(double v, double min, double max, ui::Color colour);

/// A key name in the help line.
ui::Element key(const std::string& k);

/// A box with a title.
ui::Element panel(const std::string& title, ui::Element content);

/// "‹ arc ›  three colours as a gradient"…
std::string_view mode_description(Mode mode);

}  // namespace wpc::tui
