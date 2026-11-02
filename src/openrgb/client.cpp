#include "openrgb/client.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "core/arc.hpp"
#include "util/error.hpp"
#include "util/utf8.hpp"

namespace wpc::openrgb {

namespace {

[[noreturn]] void protocol_error(const std::string& message) {
    throw Error("OpenRGB protocol error: " + message);
}

void put_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>(v >> shift));
}

uint32_t get_u32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// Reads little-endian values from a payload, checking every read stays inside it:
/// truncated data is an error, never a crash.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : data_(data) {}

    std::span<const uint8_t> take(size_t n) {
        if (n > data_.size() - pos_) {
            protocol_error(std::format("truncated payload ({} bytes needed at offset {})", n, pos_));
        }
        std::span<const uint8_t> out = data_.subspan(pos_, n);
        pos_ += n;
        return out;
    }

    uint16_t u16() {
        std::span<const uint8_t> b = take(2);
        return static_cast<uint16_t>(b[0] | (b[1] << 8));
    }

    uint32_t u32() { return get_u32(take(4).data()); }
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

}  // namespace

// ---------------------------------------------------------------------------------------------
// Painting

Rgb scale(Rgb c, std::array<float, 3> gain) {
    auto s = [](uint8_t v, float k) {
        return static_cast<uint8_t>(std::clamp(std::round(static_cast<float>(v) * k), 0.0f, 255.0f));
    };
    return Rgb{s(c.r, gain[0]), s(c.g, gain[1]), s(c.b, gain[2])};
}

std::vector<Rgb> Controller::paint(const std::vector<Rgb>& stops) const {
    return paint_calibrated(stops, [](std::string_view) { return std::array<float, 3>{1, 1, 1}; });
}

std::vector<Rgb> Controller::paint_calibrated(const std::vector<Rgb>& stops, const GainFn& gain) const {
    bool ring = kind != DEVICE_TYPE_DRAM;
    uint32_t zoned = 0;
    for (const Zone& z : zones) zoned += z.leds;

    std::vector<Rgb> out;
    if (zoned != led_count()) {
        // The zones do not add up: one gradient over all LEDs.
        std::array<float, 3> g = gain("");
        for (Rgb c : gradient(stops, led_count(), ring)) out.push_back(scale(c, g));
        return out;
    }
    for (const Zone& z : zones) {
        std::array<float, 3> g = gain(z.name);
        for (Rgb c : gradient(stops, z.leds, ring)) out.push_back(scale(c, g));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Decoding REQUEST_CONTROLLER_DATA

Controller parse_controller(std::span<const uint8_t> data, uint32_t index, uint32_t proto) {
    Reader r(data);
    Controller c;
    c.index = index;
    r.u32();  // size
    c.kind = r.i32();
    c.name = r.string();
    if (proto >= 1) c.vendor = r.string();
    c.description = r.string();
    c.version = r.string();
    c.serial = r.string();
    c.location = r.string();

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
        zone.kind = r.i32();
        r.skip(4 + 4);  // leds_min, leds_max
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
    for (uint16_t i = 0; i < color_count; ++i) {
        std::span<const uint8_t> rgbx = r.take(4);
        c.colors.push_back(Rgb{rgbx[0], rgbx[1], rgbx[2]});
    }
    return c;
}

// ---------------------------------------------------------------------------------------------
// Client

Client Client::connect(std::string_view address, std::string_view client_name) {
    Fd socket = tcp_connect(address, std::chrono::seconds(2));
    set_timeouts(socket, Seconds(5), Seconds(5));
    Client client(std::move(socket));

    std::vector<uint8_t> name(client_name.begin(), client_name.end());
    name.push_back(0);
    client.send(0, packet::SET_CLIENT_NAME, name);

    std::vector<uint8_t> version;
    put_u32(version, PROTOCOL_VERSION);
    client.send(0, packet::REQUEST_PROTOCOL_VERSION, version);
    std::vector<uint8_t> reply = client.receive(packet::REQUEST_PROTOCOL_VERSION);
    uint32_t server = Reader(reply).u32();
    client.protocol_ = std::min(server, PROTOCOL_VERSION);
    return client;
}

uint32_t Client::controller_count() {
    send(0, packet::REQUEST_CONTROLLER_COUNT, {});
    std::vector<uint8_t> reply = receive(packet::REQUEST_CONTROLLER_COUNT);
    return Reader(reply).u32();
}

Controller Client::controller(uint32_t index) {
    std::vector<uint8_t> version;
    put_u32(version, protocol_);
    send(index, packet::REQUEST_CONTROLLER_DATA, version);
    std::vector<uint8_t> reply = receive(packet::REQUEST_CONTROLLER_DATA);
    if (reply.empty()) protocol_error(std::format("no controller with index {}", index));
    return parse_controller(reply, index, protocol_);
}

std::vector<Controller> Client::controllers() {
    uint32_t count = controller_count();
    std::vector<Controller> out;
    for (uint32_t i = 0; i < count; ++i) out.push_back(controller(i));
    return out;
}

void Client::set_leds(uint32_t index, const std::vector<Rgb>& colors) {
    if (colors.size() > 0xFFFF) protocol_error(std::format("too many LEDs: {}", colors.size()));
    size_t size = 4 + 2 + 4 * colors.size();
    std::vector<uint8_t> payload;
    payload.reserve(size);
    put_u32(payload, static_cast<uint32_t>(size));
    put_u16(payload, static_cast<uint16_t>(colors.size()));
    for (Rgb c : colors) payload.insert(payload.end(), {c.r, c.g, c.b, 0});
    send(index, packet::UPDATE_LEDS, payload);
}

void Client::set_all(const Controller& controller, Rgb color) {
    set_leds(controller.index, std::vector<Rgb>(controller.led_count(), color));
}

void Client::send(uint32_t device, uint32_t id, std::span<const uint8_t> payload) {
    std::vector<uint8_t> buffer = {'O', 'R', 'G', 'B'};
    put_u32(buffer, device);
    put_u32(buffer, id);
    put_u32(buffer, static_cast<uint32_t>(payload.size()));
    buffer.insert(buffer.end(), payload.begin(), payload.end());
    write_all(socket_, buffer);
}

std::vector<uint8_t> Client::receive(uint32_t want) {
    while (true) {
        uint8_t header[16];
        read_exact(socket_, header);
        if (!std::equal(header, header + 4, "ORGB")) protocol_error("bad packet magic");
        uint32_t id = get_u32(header + 8);
        uint32_t size = get_u32(header + 12);
        if (size > (16u << 20)) protocol_error(std::format("packet too large: {} bytes", size));
        std::vector<uint8_t> payload(size);
        read_exact(socket_, payload);
        if (id == want) return payload;
        if (id != packet::DEVICE_LIST_UPDATED) {
            protocol_error(std::format("expected packet {}, got {}", want, id));
        }
    }
}

}  // namespace wpc::openrgb
