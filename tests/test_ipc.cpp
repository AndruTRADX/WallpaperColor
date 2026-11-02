#include <doctest/doctest.h>

#include "core/ipc.hpp"
#include "util/error.hpp"

using namespace wpc;
using namespace wpc::ipc;

TEST_CASE("wire format is stable json") {
    CHECK(to_json(Request::Status) == R"({"cmd":"status"})");
    CHECK(to_json(Response::ok()) == R"({"result":"ok"})");
    CHECK(to_json(Response::error("x")) == R"({"result":"error","message":"x"})");
}

TEST_CASE("status round trip") {
    Status s;
    s.version = "0.1.0";
    s.mode = Mode::Arc;
    s.stops = {"#ff0000"};
    s.devices = {DeviceInfo{"RAM", 1, {ZoneInfo{"DIMM", 10}}}};
    std::string json = to_json(Response::of(s));
    CHECK(response_from_json(json) == Response::of(s));
}

TEST_CASE("same bytes as the Rust version") {
    // tests/golden/ipc.txt, lines 7 and 8
    CHECK(to_json(Response::of(Status{})) ==
          R"({"result":"status","version":"","paused":false,"mode":"arc","plugin":"","image":null,"stops":[],"devices":[],"last_sent":null,"error":null})");
    Status full;
    full.version = "0.1.0";
    full.paused = true;
    full.mode = Mode::Single;
    full.plugin = "org.kde.slideshow";
    full.image = "/home/u/Pictures/wallpapers/a \"b\" é.png";
    full.stops = {"#4d96ff", "#a6cbff", "#234473"};
    full.devices = {DeviceInfo{"iCUE COMMANDER Core", 7, {ZoneInfo{"H150i", 29}, ZoneInfo{"8-LED Series Fan", 8}}}};
    full.last_sent = 1793644307;
    full.error = "cannot reach\tthe server";
    CHECK(to_json(Response::of(full)) ==
          R"({"result":"status","version":"0.1.0","paused":true,"mode":"single","plugin":"org.kde.slideshow","image":"/home/u/Pictures/wallpapers/a \"b\" é.png","stops":["#4d96ff","#a6cbff","#234473"],"devices":[{"name":"iCUE COMMANDER Core","kind":7,"zones":[{"name":"H150i","leds":29},{"name":"8-LED Series Fan","leds":8}]}],"last_sent":1793644307,"error":"cannot reach\tthe server"})");
}

TEST_CASE("requests parse like serde") {
    CHECK(request_from_json(R"({"cmd":"status","extra":1})") == Request::Status);
    CHECK(request_from_json(R"({"cmd":"apply"})") == Request::Apply);
    try {
        request_from_json(R"({"cmd":"nope"})");
        FAIL("parsed");
    } catch (const Error& e) {
        CHECK(std::string(e.what()) == "unknown variant `nope`, expected one of `status`, `pause`, `resume`, `apply`");
    }
    CHECK_THROWS_AS(request_from_json("garbage"), Error);
    CHECK_THROWS_AS(request_from_json(""), Error);
}

TEST_CASE("invalid utf-8 cannot be sent") {
    Status s;
    s.image = std::string("/bad/\xff.png");
    CHECK_THROWS(to_json(Response::of(s)));
}
