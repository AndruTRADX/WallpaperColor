#include <doctest/doctest.h>

#include <string>

#include <sys/socket.h>

#include "shared/infrastructure/services/protocol_json.hpp"
#include "utils/error.hpp"
#include "utils/socket.hpp"

using namespace wpc;
using namespace wpc::ipc;

TEST_CASE("wire format is stable json") {
    CHECK(ProtocolJson().encode(Request::Status) == R"({"cmd":"status"})");
    CHECK(ProtocolJson().encode(Response::ok()) == R"({"result":"ok"})");
    CHECK(ProtocolJson().encode(Response::error("x")) == R"({"result":"error","message":"x"})");
}

TEST_CASE("status round trip") {
    Status s;
    s.version = "0.1.0";
    s.mode = Mode::Arc;
    s.stops = {"#ff0000"};
    s.devices = {DeviceInfo{"RAM", "openrgb", {ZoneInfo{"DIMM", 10, Component::Ram, Shape::Line}}}};
    s.drivers = {DriverState{"openrgb", {Component::Fans, Component::Ram}, true, {}},
                 DriverState{"fusion2", {Component::Motherboard}, false, "cannot open /dev/hidraw7"}};
    std::string json = ProtocolJson().encode(Response::of(s));
    CHECK(ProtocolJson().decode_response(json) == Response::of(s));
}

TEST_CASE("status json, empty and full") {
    CHECK(ProtocolJson().encode(Response::of(Status{})) ==
          R"({"result":"status","version":"","paused":false,"testing":false,"mode":"arc","plugin":"","image":null,"stops":[],"devices":[],"drivers":[],"last_sent":null,"error":null})");
    Status full;
    full.version = "0.1.0";
    full.paused = true;
    full.mode = Mode::Single;
    full.plugin = "org.kde.slideshow";
    full.image = "/home/u/Pictures/wallpapers/a \"b\" é.png";
    full.stops = {"#4d96ff", "#a6cbff", "#234473"};
    full.devices = {DeviceInfo{"iCUE COMMANDER Core", "openrgb",
                               {ZoneInfo{"H150i", 29, Component::Pump, Shape::Ring},
                                ZoneInfo{"8-LED Series Fan", 8, Component::Fans, Shape::Ring}}}};
    full.drivers = {DriverState{"fusion2", {Component::Motherboard, Component::Argb}, true, {}}};
    full.last_sent = 1793644307;
    full.error = "cannot reach\tthe server";
    CHECK(ProtocolJson().encode(Response::of(full)) ==
          R"({"result":"status","version":"0.1.0","paused":true,"testing":false,"mode":"single","plugin":"org.kde.slideshow","image":"/home/u/Pictures/wallpapers/a \"b\" é.png","stops":["#4d96ff","#a6cbff","#234473"],"devices":[{"name":"iCUE COMMANDER Core","driver":"openrgb","zones":[{"name":"H150i","leds":29,"component":"pump","shape":"ring"},{"name":"8-LED Series Fan","leds":8,"component":"fans","shape":"ring"}]}],"drivers":[{"name":"fusion2","components":["motherboard","argb"],"loaded":true,"error":null}],"last_sent":1793644307,"error":"cannot reach\tthe server"})");
}

TEST_CASE("requests: extra fields are ignored, unknown commands are errors") {
    CHECK(ProtocolJson().decode_request(R"({"cmd":"status","extra":1})") == Request::Status);
    CHECK(ProtocolJson().decode_request(R"({"cmd":"apply"})") == Request::Apply);
    CHECK(ProtocolJson().decode_request(R"({"cmd":"test_off"})") == Request::TestOff);
    try {
        ProtocolJson().decode_request(R"({"cmd":"nope"})");
        FAIL("parsed");
    } catch (const Error& e) {
        CHECK(std::string(e.what()) == "unknown command `nope`");
    }
    CHECK_THROWS_AS(ProtocolJson().decode_request("garbage"), Error);
    CHECK_THROWS_AS(ProtocolJson().decode_request(""), Error);
}

TEST_CASE("invalid utf-8 cannot be sent") {
    Status s;
    s.image = std::string("/bad/\xff.png");
    CHECK_THROWS(ProtocolJson().encode(Response::of(s)));
}

namespace {
/// Two connected ends of a local socket.
std::pair<Fd, Fd> socket_pair() {
    int ends[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, ends) == 0);
    return {Fd(ends[0]), Fd(ends[1])};
}
}  // namespace

TEST_CASE("a line is read up to its end, also when it takes several reads") {
    auto [reader, writer] = socket_pair();
    std::string line = std::string(10000, 'a') + "\n";
    write_all(writer, line);
    CHECK(read_line(reader) == line);

    write_all(writer, std::string("short"));
    writer = Fd(-1);  // closed: the stream ends without a '\n'
    CHECK(read_line(reader) == "short");
}

TEST_CASE("a line too long is an error, not memory without end") {
    auto [reader, writer] = socket_pair();
    write_all(writer, std::string(70000, 'b'));  // no end in sight
    CHECK_THROWS_WITH(read_line(reader, 65536), "line longer than 65536 bytes");
}
