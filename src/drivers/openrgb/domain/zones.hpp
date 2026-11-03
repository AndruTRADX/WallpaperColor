// What each zone of an OpenRGB controller is, from its device type and the zone's name.
//   RAM: `ram`, painted top to bottom; a zone with "fan" in its name: `fans`; with "pump":
//   `pump`; any other zone of a cooler (an AIO's head, like "H150i ELITE CAPELLIX XT"): `pump`;
//   motherboards, GPUs, LED strips, keyboards by their device type; the rest `other`.
// Rings unless the thing is a line (RAM, strips, keyboards). When the zones don't add up to the
// device's LEDs, the whole device is one zone. The user can correct it with [[component]].

#pragma once

#include <vector>

#include "drivers/openrgb/domain/controller.hpp"

namespace wpc::openrgb {

std::vector<wpc::Zone> classify(const Controller& c);

}  // namespace wpc::openrgb
