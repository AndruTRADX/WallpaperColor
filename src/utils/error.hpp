// The one error type of the project.
//
// Every function that can fail throws wpc::Error with a message ready to show to the user.
// Values that may be missing use std::optional instead.

#pragma once

#include <cstring>
#include <stdexcept>
#include <string>

#include "utils/text.hpp"

namespace wpc {

// Visible even in driver modules (built with hidden symbols), so an Error thrown in a module
// is caught as wpc::Error in the program.
struct [[gnu::visibility("default")]] Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// The text for errno `code`: "No such file or directory (os error 2)".
inline std::string os_error(int code) {
    return cat(std::strerror(code), " (os error ", code, ")");
}

}  // namespace wpc
