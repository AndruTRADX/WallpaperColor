#include "drivers/openrgb/infrastructure/services/client.hpp"

#include <algorithm>
#include <cmath>

#include "utils/error.hpp"
#include "utils/utf8.hpp"

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
    if (reply.empty()) protocol_error(cat("no controller with index ", index));
    return parse_controller(reply, index, protocol_);
}

std::vector<Controller> Client::controllers() {
    uint32_t count = controller_count();
    std::vector<Controller> out;
    for (uint32_t i = 0; i < count; ++i) out.push_back(controller(i));
    return out;
}

void Client::set_leds(uint32_t index, std::span<const Rgb> colors) {
    if (colors.size() > 0xFFFF) protocol_error(cat("too many LEDs: ", colors.size()));
    // size of the whole payload, LED count, then red, green, blue and a padding byte per LED
    size_t size = 4 + 2 + 4 * colors.size();
    start_packet(index, packet::UPDATE_LEDS, size);
    put_u32(out_, static_cast<uint32_t>(size));
    put_u16(out_, static_cast<uint16_t>(colors.size()));
    for (Rgb c : colors) out_.insert(out_.end(), {c.r, c.g, c.b, 0});
    write_all(socket_, out_);
}

void Client::set_zone_leds(uint32_t index, uint32_t zone, std::span<const Rgb> colors) {
    if (colors.size() > 0xFFFF) protocol_error(cat("too many LEDs: ", colors.size()));
    // the same as set_leds, with the zone number after the size
    size_t size = 4 + 4 + 2 + 4 * colors.size();
    start_packet(index, packet::UPDATE_ZONE_LEDS, size);
    put_u32(out_, static_cast<uint32_t>(size));
    put_u32(out_, zone);
    put_u16(out_, static_cast<uint16_t>(colors.size()));
    for (Rgb c : colors) out_.insert(out_.end(), {c.r, c.g, c.b, 0});
    write_all(socket_, out_);
}

void Client::start_packet(uint32_t device, uint32_t id, size_t payload_size) {
    out_.clear();  // keeps its memory: a frame's packets need no allocation
    out_.reserve(16 + payload_size);
    out_.insert(out_.end(), {'O', 'R', 'G', 'B'});
    put_u32(out_, device);
    put_u32(out_, id);
    put_u32(out_, static_cast<uint32_t>(payload_size));
}

void Client::send(uint32_t device, uint32_t id, std::span<const uint8_t> payload) {
    start_packet(device, id, payload.size());
    out_.insert(out_.end(), payload.begin(), payload.end());
    write_all(socket_, out_);
}

std::vector<uint8_t> Client::receive(uint32_t want) {
    while (true) {
        uint8_t header[16];
        read_exact(socket_, header);
        if (!std::equal(header, header + 4, "ORGB")) protocol_error("bad packet magic");
        uint32_t id = get_u32(header + 8);
        uint32_t size = get_u32(header + 12);
        if (size > (16u << 20)) protocol_error(cat("packet too large: ", size, " bytes"));
        std::vector<uint8_t> payload(size);
        read_exact(socket_, payload);
        if (id == want) return payload;
        if (id != packet::DEVICE_LIST_UPDATED) {
            protocol_error(cat("expected packet ", want, ", got ", id));
        }
    }
}

}  // namespace wpc::openrgb
