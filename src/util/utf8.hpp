// UTF-8 helpers.

#pragma once

#include <string>
#include <string_view>

namespace wpc {

/// Rust's String::from_utf8_lossy: valid UTF-8 is kept, every invalid sequence becomes
/// U+FFFD (the "replacement character", shown as �).
std::string utf8_lossy(std::string_view bytes);

/// Whether `text` is valid UTF-8.
bool is_valid_utf8(std::string_view text);

}  // namespace wpc
