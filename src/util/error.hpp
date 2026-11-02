// The one error type of the project.
//
// Every function that can fail throws wpc::Error with a message ready to show to the user.
// Callers catch it exactly where the Rust version matched on `Err(...)`.

#pragma once

#include <cstring>
#include <format>
#include <stdexcept>
#include <string>

namespace wpc {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// The text for errno `code`, worded like Rust's io::Error:
/// "No such file or directory (os error 2)".
inline std::string os_error(int code) {
    return std::format("{} (os error {})", std::strerror(code), code);
}

}  // namespace wpc
