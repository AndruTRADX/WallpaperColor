// What each device gets: from the devices a driver found, the chosen components and the colours,
// which zones are painted and with which colours. Pure: no driver is called here.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "drivers/sdk/hardware.hpp"
#include "shared/domain/colours/paint.hpp"
#include "shared/models/protocol/messages.hpp"
#include "shared/domain/settings/settings.hpp"

namespace wpc {

/// The chosen zones of one device and their new colours.
struct Output {
    std::string driver;
    /// The device's number in the driver's devices().
    size_t device = 0;
    /// Fades remember what an output shows under this name.
    std::string key;
    /// Zone number and LED count of every chosen zone, in order.
    std::vector<std::pair<size_t, size_t>> zones;
    /// The new colours, one per LED of those zones.
    std::vector<Rgb> target;
    /// What the device says those LEDs show now; empty if it can't tell.
    std::vector<Rgb> reported;
};

/// Adds the outputs of `driver`'s `devices` to `outputs`, and every device (all zones, chosen
/// or not, with what each zone is) to `found`.
void plan_outputs(const std::string& driver, const std::vector<Device>& devices, const ComponentSettings& components,
                  Painter& painter, std::vector<Output>& outputs, std::vector<ipc::DeviceInfo>& found);

}  // namespace wpc
