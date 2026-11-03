// Service: KDE's colour scheme, read from kdeglobals: dark when the window background is darker
// than mid-grey.

#pragma once

#include <string_view>

#include "daemon/application/contracts/colour_scheme.hpp"

namespace wpc {

class KdeColourScheme final : public ColourScheme {
public:
    bool is_dark() override;
    /// The same test on the text of a kdeglobals file.
    static bool is_dark_text(std::string_view kdeglobals);
};

}  // namespace wpc
