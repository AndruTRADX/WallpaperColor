#include "drivers/openrgb/domain/zones.hpp"

#include "utils/text.hpp"

namespace wpc::openrgb {

namespace {

wpc::Zone classify_zone(const Controller& c, std::string name, uint32_t leds) {
    wpc::Zone z{std::move(name), leds, Component::Other, Shape::Ring};
    switch (c.kind) {
        case device_type::DRAM: z.component = Component::Ram; z.shape = Shape::Line; break;
        case device_type::MOTHERBOARD: z.component = Component::Motherboard; break;
        case device_type::GPU: z.component = Component::Gpu; break;
        case device_type::COOLER: z.component = Component::Pump; break;  // fans are found below
        case device_type::LED_STRIP: z.component = Component::Strip; z.shape = Shape::Line; break;
        case device_type::KEYBOARD: z.component = Component::Keyboard; z.shape = Shape::Line; break;
        default: break;
    }
    if (c.kind != device_type::DRAM) {
        if (contains_ignoring_case(z.name, "fan")) z.component = Component::Fans;
        if (contains_ignoring_case(z.name, "pump")) z.component = Component::Pump;
    }
    return z;
}

}  // namespace

std::vector<wpc::Zone> classify(const Controller& c) {
    uint32_t zoned = 0;
    for (const Zone& z : c.zones) zoned += z.leds;
    if (zoned != c.led_count() || c.zones.empty()) {
        return {classify_zone(c, "", static_cast<uint32_t>(c.led_count()))};
    }
    std::vector<wpc::Zone> out;
    for (const Zone& z : c.zones) out.push_back(classify_zone(c, z.name, z.leds));
    return out;
}

}  // namespace wpc::openrgb
