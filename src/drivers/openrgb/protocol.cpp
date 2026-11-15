#include "drivers/openrgb/protocol.hpp"

#include "utils/error.hpp"
#include "utils/text.hpp"
#include "utils/utf8.hpp"

namespace wpc::openrgb {

void protocol_error(const std::string& message) {
    throw Error("OpenRGB protocol error: " + message);
}

namespace {

/// Reads little-endian values from a payload, checking every read stays inside it:
/// truncated data is an error, never a crash.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : data_(data) {}

    std::span<const uint8_t> take(size_t n) {
        if (n > data_.size() - pos_) {
            protocol_error(cat("truncated payload (", n, " bytes needed at offset ", pos_, ")"));
        }
        std::span<const uint8_t> out = data_.subspan(pos_, n);
        pos_ += n;
        return out;
    }

    uint16_t u16() {
        std::span<const uint8_t> b = take(2);
        return static_cast<uint16_t>(b[0] | (b[1] << 8));
    }

    uint32_t u32() {
        std::span<const uint8_t> b = take(4);
        return b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<uint32_t>(b[3]) << 24);
    }

    int32_t i32() { return static_cast<int32_t>(u32()); }

    std::string string() {
        size_t length = u16();
        std::span<const uint8_t> bytes = take(length);
        if (!bytes.empty() && bytes.back() == 0) bytes = bytes.first(bytes.size() - 1);
        return utf8_lossy(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }

    void skip(size_t n) { take(n); }

private:
    std::span<const uint8_t> data_;
    size_t pos_ = 0;
};

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

// ---------------------------------------------------------------------------------------------
// Decoding REQUEST_CONTROLLER_DATA

Controller parse_controller(std::span<const uint8_t> data, uint32_t index, uint32_t proto) {
    Reader r(data);
    Controller c;
    c.index = index;
    r.u32();  // size
    c.kind = r.i32();
    c.name = r.string();
    if (proto >= 1) r.string();  // vendor
    for (int i = 0; i < 4; ++i) r.string();  // description, version, serial, location

    uint16_t modes = r.u16();
    r.i32();  // active mode
    for (uint16_t m = 0; m < modes; ++m) {
        r.string();                    // name
        r.skip(4 + 4 + 4 + 4);         // value, flags, speed_min, speed_max
        if (proto >= 3) r.skip(4 + 4);  // brightness_min, brightness_max
        r.skip(4 + 4 + 4);             // colors_min, colors_max, speed
        if (proto >= 3) r.skip(4);     // brightness
        r.skip(4 + 4);                 // direction, color_mode
        size_t colors = r.u16();
        r.skip(4 * colors);
    }

    uint16_t zone_count = r.u16();
    for (uint16_t z = 0; z < zone_count; ++z) {
        Zone zone;
        zone.name = r.string();
        r.skip(4 + 4 + 4);  // type, leds_min, leds_max
        zone.leds = r.u32();
        size_t matrix_length = r.u16();  // bytes: height + width + entries
        r.skip(matrix_length);
        if (proto >= 4) {
            uint16_t segments = r.u16();
            for (uint16_t s = 0; s < segments; ++s) {
                r.string();
                r.skip(4 + 4 + 4);  // type, start, count
            }
        }
        c.zones.push_back(std::move(zone));
    }

    uint16_t leds = r.u16();
    for (uint16_t i = 0; i < leds; ++i) {
        r.string();
        r.skip(4);  // value
    }

    uint16_t color_count = r.u16();
    c.colors.reserve(color_count);
    for (uint16_t i = 0; i < color_count; ++i) {
        std::span<const uint8_t> rgbx = r.take(4);
        c.colors.push_back(Rgb{rgbx[0], rgbx[1], rgbx[2]});
    }
    return c;
}

// ---------------------------------------------------------------------------------------------
// Zones and settings

std::vector<wpc::Zone> classify(const Controller& c) {
    uint64_t zoned = 0;
    for (const Zone& z : c.zones) zoned += z.leds;
    if (zoned != c.led_count() || c.zones.empty()) {
        return {classify_zone(c, "", static_cast<uint32_t>(c.led_count()))};
    }
    std::vector<wpc::Zone> out;
    for (const Zone& z : c.zones) out.push_back(classify_zone(c, z.name, z.leds));
    return out;
}

ClientSettings read_settings(const DriverSettings& table) {
    ClientSettings s;
    if (std::optional<std::string> address = table.string("address")) s.address = *address;
    if (std::optional<std::string> name = table.string("client_name")) s.client_name = *name;
    return s;
}

}  // namespace wpc::openrgb
