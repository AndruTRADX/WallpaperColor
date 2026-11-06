// What the fusion2 driver needs from the board's RGB controller.

#pragma once

#include <vector>

#include "drivers/fusion2/domain/settings.hpp"
#include "drivers/sdk/hardware.hpp"

namespace wpc::fusion2 {

class FusionBoard {
public:
    virtual ~FusionBoard() = default;
    /// Finds the controller and opens it for `settings`; false if this computer has none.
    /// Throws wpc::Error if it is there but cannot be used (no permission).
    virtual bool open(const BoardSettings& settings) = 0;
    /// Closes it (opened again by the next open()).
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    /// False when the controller opened was unplugged since (plugged in again, it is another
    /// device: the open one only answers "no such device").
    virtual bool still_connected() const = 0;
    /// Shows `colour` on every single-colour zone. Throws wpc::Error.
    virtual void set_onboard(Rgb colour) = 0;
    /// Shows `colours` on the LEDs of ARGB header `header` (0 or 1), in order. Throws wpc::Error.
    virtual void set_header(int header, const std::vector<Rgb>& colours) = 0;
};

}  // namespace wpc::fusion2
