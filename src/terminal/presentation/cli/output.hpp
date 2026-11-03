// Printing for the commands: lines on stdout, colour swatches, joined lists.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "shared/domain/colours/color.hpp"

namespace wpc {

/// Prints one line to stdout. Exits quietly when the reader went away
/// (`wallpapercolor ... | head`) instead of failing on a broken pipe.
void out(const std::string& line);

/// A block of `width` cells in the colour (24-bit ANSI background), only on an interactive
/// terminal so piped output stays plain text.
std::string swatch(Rgb c, int width);

/// `items` joined with `separator`.
std::string join(const std::vector<std::string>& items, std::string_view separator);

}  // namespace wpc
