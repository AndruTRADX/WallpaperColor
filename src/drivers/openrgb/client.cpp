#include "drivers/openrgb/client.hpp"

#include <algorithm>
#include <cmath>

#include "utils/error.hpp"
#include "utils/text.hpp"

namespace wpc::openrgb {

namespace {

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

}  // namespace

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
    if (reply.size() < 4) protocol_error("truncated protocol version");
    uint32_t server = get_u32(reply.data());
    client.protocol_ = std::min(server, PROTOCOL_VERSION);
    return client;
}

uint32_t Client::controller_count() {
    send(0, packet::REQUEST_CONTROLLER_COUNT, {});
    std::vector<uint8_t> reply = receive(packet::REQUEST_CONTROLLER_COUNT);
    if (reply.size() < 4) protocol_error("truncated controller count");
    return get_u32(reply.data());
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
    // Each one is a round trip: a count no real server sends would keep the service busy.
    if (count > 1024) protocol_error(cat("too many controllers: ", count));
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
