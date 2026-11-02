// Tests against an in-process fake SDK server that encodes controllers like OpenLinkHub does.

#include <doctest/doctest.h>

#include <mutex>
#include <string>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "openrgb/client.hpp"
#include "util/error.hpp"

using namespace wpc;
using namespace wpc::openrgb;

namespace {

struct FakeController {
    std::string name;
    std::vector<std::pair<std::string, uint32_t>> zones;
};

void u16le(std::vector<uint8_t>& b, uint16_t v) { b.insert(b.end(), {uint8_t(v), uint8_t(v >> 8)}); }
void u32le(std::vector<uint8_t>& b, uint32_t v) {
    b.insert(b.end(), {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)});
}
void string(std::vector<uint8_t>& b, const std::string& s) {
    u16le(b, uint16_t(s.size() + 1));
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}

/// Controller data for protocol 4, same layout as OpenLinkHub's buildDeviceDataPayload.
std::vector<uint8_t> encode(const FakeController& c) {
    std::vector<uint8_t> b;
    u32le(b, 7);  // device type
    string(b, c.name);
    for (const char* s : {"Corsair", "desc", "1.0", "SERIAL", "HID: /dev/hidraw8"}) string(b, s);
    u16le(b, 1);  // one mode
    u32le(b, 0);  // active mode
    string(b, "Direct");
    for (uint32_t v : {0u, 1u << 5, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u}) u32le(b, v);
    u16le(b, 0);  // mode colours
    u16le(b, uint16_t(c.zones.size()));
    uint32_t total = 0;
    for (const auto& [name, leds] : c.zones) {
        string(b, name);
        u32le(b, 1);  // zone type
        for (int i = 0; i < 3; ++i) u32le(b, leds);  // min, max, count
        u16le(b, 12);  // matrix: 1x1
        u32le(b, 1);
        u32le(b, 1);
        u32le(b, 0);
        u16le(b, 1);  // one segment
        string(b, "seg");
        for (uint32_t v : {0u, 0u, leds}) u32le(b, v);
        total += leds;
    }
    u16le(b, uint16_t(total));
    for (uint32_t i = 0; i < total; ++i) {
        string(b, "LED " + std::to_string(i));
        u32le(b, i);
    }
    u16le(b, uint16_t(total));
    for (uint32_t i = 0; i < total; ++i) b.insert(b.end(), {0, 255, 255, 0});
    std::vector<uint8_t> out;
    u32le(out, uint32_t(b.size() + 4));
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

std::vector<FakeController> devices() {
    return {
        {"VENGEANCE RGB", {{"DIMM", 10}}},
        {"iCUE COMMANDER Core", {{"AIO", 29}, {"Fan 1", 8}, {"Fan 2", 24}}},
    };
}

struct Received {
    uint32_t device;
    uint32_t id;
    std::vector<uint8_t> payload;
};

/// A fake server on a free local port, answering one client in a thread.
class FakeServer {
public:
    FakeServer(std::vector<FakeController> controllers, uint32_t server_proto)
        : controllers_(std::move(controllers)), proto_(server_proto) {
        listener_ = Fd(::socket(AF_INET, SOCK_STREAM, 0));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(listener_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        ::listen(listener_.get(), 1);
        socklen_t len = sizeof addr;
        ::getsockname(listener_.get(), reinterpret_cast<sockaddr*>(&addr), &len);
        address_ = "127.0.0.1:" + std::to_string(ntohs(addr.sin_port));
        thread_ = std::thread([this] { serve(); });
    }
    ~FakeServer() {
        if (thread_.joinable()) thread_.join();
    }

    const std::string& address() const { return address_; }

    /// Everything the client sent (call after the client is gone: waits for the server).
    std::vector<Received> received() {
        thread_.join();
        return received_;
    }

private:
    void reply(const Fd& s, uint32_t device, uint32_t id, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> buf = {'O', 'R', 'G', 'B'};
        u32le(buf, device);
        u32le(buf, id);
        u32le(buf, uint32_t(payload.size()));
        buf.insert(buf.end(), payload.begin(), payload.end());
        write_all(s, buf);
    }

    void serve() {
        Fd s(::accept(listener_.get(), nullptr, nullptr));
        try {
            while (true) {
                uint8_t h[16];
                read_exact(s, h);
                uint32_t device = h[4] | (h[5] << 8) | (h[6] << 16) | (uint32_t(h[7]) << 24);
                uint32_t id = h[8] | (h[9] << 8) | (h[10] << 16) | (uint32_t(h[11]) << 24);
                uint32_t size = h[12] | (h[13] << 8) | (h[14] << 16) | (uint32_t(h[15]) << 24);
                std::vector<uint8_t> p(size);
                read_exact(s, p);
                if (id == packet::REQUEST_PROTOCOL_VERSION) {
                    // an unsolicited notification first, which the client must skip
                    reply(s, 0, packet::DEVICE_LIST_UPDATED, {});
                    std::vector<uint8_t> v;
                    u32le(v, proto_);
                    reply(s, 0, id, v);
                } else if (id == packet::REQUEST_CONTROLLER_COUNT) {
                    std::vector<uint8_t> v;
                    u32le(v, uint32_t(controllers_.size()));
                    reply(s, 0, id, v);
                } else if (id == packet::REQUEST_CONTROLLER_DATA) {
                    reply(s, device, id, device < controllers_.size() ? encode(controllers_[device]) : std::vector<uint8_t>{});
                }
                received_.push_back({device, id, p});
            }
        } catch (const Error&) {
            // the client closed the connection
        }
    }

    std::vector<FakeController> controllers_;
    uint32_t proto_;
    Fd listener_;
    std::string address_;
    std::vector<Received> received_;
    std::thread thread_;
};

}  // namespace

TEST_CASE("lists controllers") {
    FakeServer server(devices(), 4);
    {
        Client c = Client::connect(server.address(), "test");
        CHECK(c.protocol() == 4);
        std::vector<Controller> ctrls = c.controllers();
        REQUIRE(ctrls.size() == 2);
        CHECK(ctrls[0].name == "VENGEANCE RGB");
        CHECK(ctrls[0].vendor == "Corsair");
        CHECK(ctrls[0].led_count() == 10);
        CHECK(ctrls[0].colors[0] == Rgb{0, 255, 255});
        CHECK(ctrls[1].name == "iCUE COMMANDER Core");
        CHECK(ctrls[1].led_count() == 61);
        REQUIRE(ctrls[1].zones.size() == 3);
        CHECK(ctrls[1].zones[0].name == "AIO");
        CHECK(ctrls[1].zones[0].leds == 29);
        CHECK(ctrls[1].zones[1].name == "Fan 1");
        CHECK(ctrls[1].zones[1].leds == 8);
        CHECK(ctrls[1].zones[2].name == "Fan 2");
        CHECK(ctrls[1].zones[2].leds == 24);
        CHECK(ctrls[1].index == 1);
    }
}

TEST_CASE("calibration scales only the matching zones") {
    FakeServer server(devices(), 4);
    Client c = Client::connect(server.address(), "test");
    Controller cc = c.controller(1);
    std::vector<Rgb> leds = cc.paint({WHITE}, [](std::string_view zone) {
        return zone.starts_with("Fan") ? std::array<float, 3>{0.72f, 0.78f, 1.0f}
                                       : std::array<float, 3>{1, 1, 1};
    });
    REQUIRE(leds.size() == 61);
    for (size_t i = 0; i < 29; ++i) CHECK(leds[i] == WHITE);                // AIO untouched
    for (size_t i = 29; i < 61; ++i) CHECK(leds[i] == Rgb{184, 199, 255});  // fans corrected
}

TEST_CASE("negotiates down to newest common version") {
    FakeServer server({}, 6);
    CHECK(Client::connect(server.address(), "test").protocol() == 4);
}

TEST_CASE("sends client name and led colours") {
    FakeServer server(devices(), 4);
    {
        Client c = Client::connect(server.address(), "WallpaperColor");
        Controller ctrl = c.controller(0);
        c.set_leds(ctrl.index, std::vector<Rgb>(ctrl.led_count(), Rgb{255, 0, 0}));
    }
    std::vector<Received> packets = server.received();

    auto find = [&](uint32_t id) {
        for (const Received& r : packets) {
            if (r.id == id) return r;
        }
        FAIL("packet not sent");
        return Received{};
    };
    Received name = find(packet::SET_CLIENT_NAME);
    CHECK(std::string(name.payload.begin(), name.payload.end()) == std::string("WallpaperColor\0", 15));

    Received leds = find(packet::UPDATE_LEDS);
    const std::vector<uint8_t>& p = leds.payload;
    CHECK(leds.device == 0);
    REQUIRE(p.size() == 6 + 10 * 4);
    CHECK((p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)) == int(p.size()));
    CHECK((p[4] | (p[5] << 8)) == 10);
    for (size_t i = 6; i < p.size(); i += 4) {
        CHECK(std::vector<uint8_t>(p.begin() + i, p.begin() + i + 4) == std::vector<uint8_t>{255, 0, 0, 0});
    }
}

TEST_CASE("missing controller is an error") {
    FakeServer server(devices(), 4);
    Client c = Client::connect(server.address(), "test");
    try {
        c.controller(5);
        FAIL("no error");
    } catch (const Error& e) {
        CHECK(std::string(e.what()).starts_with("OpenRGB protocol error"));
    }
}

TEST_CASE("truncated data is an error not a crash") {
    std::vector<uint8_t> data = encode(devices()[1]);
    for (size_t cut : {size_t(0), size_t(10), size_t(40), data.size() / 2, data.size() - 1}) {
        CAPTURE(cut);
        CHECK_THROWS_AS(parse_controller(std::span(data).first(cut), 0, 4), Error);
    }
    CHECK_NOTHROW(parse_controller(data, 0, 4));
}

TEST_CASE("connection refused is an os error") {
    // a port nobody listens on: bind one, read its number, close it
    Fd probe(::socket(AF_INET, SOCK_STREAM, 0));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(probe.get(), reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    socklen_t len = sizeof addr;
    ::getsockname(probe.get(), reinterpret_cast<sockaddr*>(&addr), &len);
    std::string address = "127.0.0.1:" + std::to_string(ntohs(addr.sin_port));
    probe = Fd();
    try {
        Client::connect(address, "test");
        FAIL("connected");
    } catch (const Error& e) {
        CHECK(std::string(e.what()) == "Connection refused (os error 111)");
    }
}
