#include "shared/infrastructure/services/protocol_json.hpp"

#include <cstdlib>
#include <format>

#include <nlohmann/json.hpp>

#include "utils/error.hpp"

namespace wpc {

using namespace ipc;

// ordered_json keeps fields in the order we add them (plain json would sort them).
using Json = nlohmann::ordered_json;

namespace {

constexpr std::string_view REQUEST_NAMES[] = {"status", "pause", "resume", "apply", "test", "test_off"};

Json status_fields(const Status& s) {
    Json j;
    j["version"] = s.version;
    j["paused"] = s.paused;
    j["testing"] = s.testing;
    j["mode"] = mode_name(s.mode);
    j["plugin"] = s.plugin;
    j["image"] = s.image ? Json(s.image->string()) : Json(nullptr);
    j["stops"] = s.stops;
    Json devices = Json::array();
    for (const DeviceInfo& d : s.devices) {
        Json zones = Json::array();
        for (const ZoneInfo& z : d.zones) {
            zones.push_back(Json{{"name", z.name},
                                 {"leds", z.leds},
                                 {"component", component_name(z.component)},
                                 {"shape", shape_name(z.shape)}});
        }
        devices.push_back(Json{{"name", d.name}, {"driver", d.driver}, {"zones", zones}});
    }
    j["devices"] = devices;
    Json drivers = Json::array();
    for (const DriverState& d : s.drivers) {
        Json components = Json::array();
        for (Component c : d.components) components.push_back(component_name(c));
        drivers.push_back(Json{{"name", d.name},
                               {"components", components},
                               {"loaded", d.loaded},
                               {"error", d.error ? Json(*d.error) : Json(nullptr)}});
    }
    j["drivers"] = drivers;
    j["last_sent"] = s.last_sent ? Json(*s.last_sent) : Json(nullptr);
    j["error"] = s.error ? Json(*s.error) : Json(nullptr);
    return j;
}

Component component_from(const Json& j) {
    std::string name = j.get<std::string>();
    if (std::optional<Component> c = component_from_name(name)) return *c;
    throw Error(std::format("unknown component `{}`", name));
}

Shape shape_from(const Json& j) {
    std::string name = j.get<std::string>();
    if (std::optional<Shape> s = shape_from_name(name)) return *s;
    throw Error(std::format("unknown shape `{}`", name));
}

Status status_from(const Json& j) {
    Status s;
    s.version = j.at("version").get<std::string>();
    s.paused = j.at("paused").get<bool>();
    s.testing = j.value("testing", false);
    std::string mode = j.at("mode").get<std::string>();
    if (mode == "arc") s.mode = Mode::Arc;
    else if (mode == "single") s.mode = Mode::Single;
    else if (mode == "split") s.mode = Mode::Split;
    else throw Error(std::format("unknown mode `{}`", mode));
    s.plugin = j.at("plugin").get<std::string>();
    if (j.contains("image") && !j["image"].is_null()) s.image = j["image"].get<std::string>();
    s.stops = j.at("stops").get<std::vector<std::string>>();
    for (const Json& d : j.at("devices")) {
        DeviceInfo device{d.at("name").get<std::string>(), d.at("driver").get<std::string>(), {}};
        for (const Json& z : d.at("zones")) {
            device.zones.push_back(ZoneInfo{z.at("name").get<std::string>(), z.at("leds").get<uint32_t>(),
                                            component_from(z.at("component")), shape_from(z.at("shape"))});
        }
        s.devices.push_back(std::move(device));
    }
    for (const Json& d : j.value("drivers", Json::array())) {
        DriverState driver{d.at("name").get<std::string>(), {}, d.at("loaded").get<bool>(), {}};
        for (const Json& c : d.at("components")) driver.components.push_back(component_from(c));
        if (d.contains("error") && !d["error"].is_null()) driver.error = d["error"].get<std::string>();
        s.drivers.push_back(std::move(driver));
    }
    if (j.contains("last_sent") && !j["last_sent"].is_null()) s.last_sent = j["last_sent"].get<uint64_t>();
    if (j.contains("error") && !j["error"].is_null()) s.error = j["error"].get<std::string>();
    return s;
}

Json parse(std::string_view line) {
    try {
        return Json::parse(line);
    } catch (const Json::parse_error& e) {
        throw Error(e.what());
    }
}

}  // namespace

std::string ProtocolJson::encode(Request request) const {
    Json j;
    j["cmd"] = REQUEST_NAMES[static_cast<int>(request)];
    return j.dump();
}

std::string ProtocolJson::encode(const Response& response) const {
    Json j;
    switch (response.kind) {
        case Response::Kind::Ok:
            j["result"] = "ok";
            break;
        case Response::Kind::Error:
            j["result"] = "error";
            j["message"] = response.message;
            break;
        case Response::Kind::Status:
            j["result"] = "status";
            j.update(status_fields(response.status));
            break;
    }
    return j.dump();  // throws on invalid UTF-8
}

Request ProtocolJson::decode_request(std::string_view line) const {
    Json j = parse(line);
    if (!j.is_object()) throw Error("invalid type: expected internally tagged enum Request");
    if (!j.contains("cmd")) throw Error("missing field `cmd`");
    if (!j["cmd"].is_string()) throw Error("invalid type: expected variant identifier");
    std::string cmd = j["cmd"].get<std::string>();
    for (size_t i = 0; i < std::size(REQUEST_NAMES); ++i) {
        if (cmd == REQUEST_NAMES[i]) return static_cast<Request>(i);
    }
    throw Error(std::format("unknown command `{}`", cmd));
}

Response ProtocolJson::decode_response(std::string_view line) const {
    Json j = parse(line);
    try {
        std::string result = j.at("result").get<std::string>();
        if (result == "ok") return Response::ok();
        if (result == "error") return Response::error(j.at("message").get<std::string>());
        if (result == "status") return Response::of(status_from(j));
        throw Error(std::format("unknown variant `{}`, expected one of `status`, `ok`, `error`", result));
    } catch (const Json::exception& e) {
        throw Error(e.what());
    }
}

}  // namespace wpc
