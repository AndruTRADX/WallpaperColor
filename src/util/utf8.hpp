// UTF-8 helpers.

#pragma once

#include <string>
#include <string_view>

namespace wpc {

/// Valid UTF-8 is kept, every invalid sequence becomes U+FFFD (the "replacement character",
/// shown as �). Device names from the OpenRGB server go through this.
std::string utf8_lossy(std::string_view bytes);

}  // namespace wpc
