// The razer driver as the programs see it: the module's Driver (drivers/sdk). A Razer Chroma
// keyboard (the Huntsman Mini), its keys as one zone. It only runs the module's features.

#pragma once

#include <memory>
#include <vector>

#include "drivers/razer/application/features/list_devices/list_devices.hpp"
#include "drivers/razer/application/features/show_frame/show_frame.hpp"
#include "drivers/sdk/driver.hpp"

namespace wpc::razer {

inline constexpr Component COMPONENTS[] = {Component::Keyboard};

class KeyboardDriver final : public Driver {
public:
    /// `keyboard`: the keyboard's service (module.cpp gives the real one).
    KeyboardDriver(KeyboardSettings settings, std::unique_ptr<RazerKeyboard> keyboard)
        : keyboard_(std::move(keyboard)), state_{settings} {}

    std::vector<Device> devices() override { return list_devices(*keyboard_, state_); }
    void show(size_t, std::span<const ZoneFrame> zones) override { show_frame(*keyboard_, state_, zones); }

private:
    std::unique_ptr<RazerKeyboard> keyboard_;
    KeyboardState state_;
};

}  // namespace wpc::razer
