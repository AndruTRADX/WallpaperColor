// What the razer driver needs from the keyboard.

#pragma once

#include <cstdint>
#include <span>

#include "drivers/sdk/hardware.hpp"

namespace wpc::razer {

class RazerKeyboard {
public:
    virtual ~RazerKeyboard() = default;
    /// Finds the keyboard and opens it; false if it isn't plugged in.
    /// Throws wpc::Error if it is there but cannot be used (no permission).
    virtual bool open() = 0;
    /// Closes it (opened again by the next open()).
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    /// Loads the colours of one row of keys, from the first column on. Shown by show().
    /// Throws wpc::Error.
    virtual void set_row(uint8_t row, std::span<const Rgb> colours) = 0;
    /// Shows the rows loaded so far. Throws wpc::Error.
    virtual void show() = 0;
};

}  // namespace wpc::razer
