// The razer driver: a Razer Chroma keyboard (the Huntsman Mini), its keys as one zone.
//
// RazerKeyboard is the port to the keyboard (keyboard.hpp is the real one, over /dev/hidraw;
// the tests give a fake). KeyboardDriver is the Driver the programs see: it opens the keyboard
// when needed (again after it was unplugged) and closes it on a failure (the next devices()
// opens it again).

#pragma once

#include <memory>
#include <span>
#include <vector>

#include "drivers/razer/protocol.hpp"
#include "drivers/sdk/driver.hpp"

namespace wpc::razer {

/// What the driver needs from the keyboard.
class RazerKeyboard {
public:
    virtual ~RazerKeyboard() = default;
    /// Finds the keyboard and opens it; false if it isn't plugged in.
    /// Throws wpc::Error if it is there but cannot be used (no permission).
    virtual bool open() = 0;
    /// Closes it (opened again by the next open()).
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    /// False when the keyboard opened was unplugged since (plugged in again, it is another
    /// device: the open one only answers "no such device").
    virtual bool still_connected() const = 0;
    /// Loads the colours of one row of keys, from the first column on. Shown by show().
    /// Throws wpc::Error.
    virtual void set_row(uint8_t row, std::span<const Rgb> colours) = 0;
    /// Shows the rows loaded so far. Throws wpc::Error.
    virtual void show() = 0;
};

inline constexpr Component COMPONENTS[] = {Component::Keyboard};

inline constexpr const char* DIRECTIONS[] = {"across", "down"};  // as read_settings reads them
inline constexpr DriverOption OPTIONS[] = {
    {"direction", "Direction", DIRECTIONS, "across: left to right; down: top to bottom"},
};

class KeyboardDriver final : public Driver {
public:
    /// `keyboard`: the keyboard's port (module.cpp gives the real one).
    KeyboardDriver(KeyboardSettings settings, std::unique_ptr<RazerKeyboard> keyboard)
        : settings_(settings), keyboard_(std::move(keyboard)) {}

    /// The keyboard's device when it is plugged in, none otherwise. Throws wpc::Error.
    std::vector<Device> devices() override;
    /// Every row of keys, then shown at once.
    void show(size_t device, std::span<const ZoneFrame> zones) override;

private:
    KeyboardSettings settings_;
    std::unique_ptr<RazerKeyboard> keyboard_;
};

}  // namespace wpc::razer
