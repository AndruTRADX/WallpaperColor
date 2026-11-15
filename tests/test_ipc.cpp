#include <doctest/doctest.h>

#include <string>

#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>

#include "protocol/json.hpp"
#include "utils/error.hpp"
#include "utils/socket.hpp"

using namespace wpc;
using namespace wpc::ipc;
namespace fs = std::filesystem;

TEST_CASE("wire format is stable json") {
    CHECK(ipc::encode(Request::Status) == R"({"cmd":"status"})");
    CHECK(ipc::encode(Response::ok()) == R"({"result":"ok"})");
    CHECK(ipc::encode(Response::error("x")) == R"({"result":"error","message":"x"})");
}

TEST_CASE("status round trip") {
    Status s;
    s.version = "0.1.0";
    s.mode = Mode::Arc;
    s.stops = {"#ff0000"};
    s.devices = {DeviceInfo{"RAM", "openrgb", {ZoneInfo{"DIMM", 10, Component::Ram, Shape::Line}}}};
    s.drivers = {DriverState{"openrgb", {Component::Fans, Component::Ram}, true, {}},
                 DriverState{"fusion2", {Component::Motherboard}, false, "cannot open /dev/hidraw7"}};
    std::string json = ipc::encode(Response::of(s));
    CHECK(ipc::decode_response(json) == Response::of(s));
}

TEST_CASE("status json, empty and full") {
    CHECK(ipc::encode(Response::of(Status{})) ==
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
    CHECK(ipc::encode(Response::of(full)) ==
          R"({"result":"status","version":"0.1.0","paused":true,"testing":false,"mode":"single","plugin":"org.kde.slideshow","image":"/home/u/Pictures/wallpapers/a \"b\" é.png","stops":["#4d96ff","#a6cbff","#234473"],"devices":[{"name":"iCUE COMMANDER Core","driver":"openrgb","zones":[{"name":"H150i","leds":29,"component":"pump","shape":"ring"},{"name":"8-LED Series Fan","leds":8,"component":"fans","shape":"ring"}]}],"drivers":[{"name":"fusion2","components":["motherboard","argb"],"loaded":true,"error":null}],"last_sent":1793644307,"error":"cannot reach\tthe server"})");
}

TEST_CASE("requests: extra fields are ignored, unknown commands are errors") {
    CHECK(ipc::decode_request(R"({"cmd":"status","extra":1})") == Request::Status);
    CHECK(ipc::decode_request(R"({"cmd":"apply"})") == Request::Apply);
    CHECK(ipc::decode_request(R"({"cmd":"test_off"})") == Request::TestOff);
    try {
        ipc::decode_request(R"({"cmd":"nope"})");
        FAIL("parsed");
    } catch (const Error& e) {
        CHECK(std::string(e.what()) == "unknown command `nope`");
    }
    CHECK_THROWS_AS(ipc::decode_request("garbage"), Error);
    CHECK_THROWS_AS(ipc::decode_request(""), Error);
}

TEST_CASE("a path that is not utf-8 is sent with U+FFFD, so the status still goes out") {
    Status s;
    s.image = std::string("/bad/\xff.png");
    std::string json = ipc::encode(Response::of(s));
    CHECK(json.find("\"image\":\"/bad/\xEF\xBF\xBD.png\"") != std::string::npos);  // U+FFFD, as UTF-8
    CHECK(ipc::decode_response(json).status.image == fs::path("/bad/\xEF\xBF\xBD.png"));
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

TEST_CASE("a host name is tried at each of its addresses") {
    // A server on 127.0.0.1 only: "localhost" is often ::1 first, which refuses.
    Fd listener(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(listener.get(), reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0);
    REQUIRE(::listen(listener.get(), 1) == 0);
    socklen_t len = sizeof addr;
    ::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&addr), &len);
    std::thread server([&] { Fd client(::accept(listener.get(), nullptr, nullptr)); });
    CHECK_NOTHROW(tcp_connect("localhost:" + std::to_string(ntohs(addr.sin_port)), std::chrono::seconds(2)));
    server.join();
}
