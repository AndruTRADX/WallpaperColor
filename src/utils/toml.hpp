// Small TOML text helpers: writing a value the way a config file has it, checking a text is
// valid TOML. Pure code, nothing about WallpaperColor.

#pragma once

#include <string>
#include <string_view>

namespace wpc {

/// A float as a TOML file has it: 0.7, 1.0 (never an exponent).
std::string toml_float(double value);

/// A TOML string: "text", with quotes, backslashes and control characters escaped.
std::string toml_string(std::string_view text);

/// Throws wpc::Error if `text` is not valid TOML (any keys allowed).
void check_toml(std::string_view text);

}  // namespace wpc
