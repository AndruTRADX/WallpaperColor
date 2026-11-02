#include "core/ipc.hpp"

#include <cstdlib>
#include <format>

#include <nlohmann/json.hpp>

#include "core/config.hpp"
#include "util/error.hpp"
#include "util/socket.hpp"

namespace wpc::ipc {

// ordered_json keeps fields in the order we add them (plain json would sort them).
using Json = nlohmann::ordered_json;

namespace {

constexpr std::string_view REQUEST_NAMES[] = {"status", "pause", "resume", "apply"};

Json status_fields(const Status& s) {
    Json j;
    j["version"] = s.version;
    j["paused"] = s.paused;
    j["mode"] = mode_name(s.mode);
    j["plugin"] = s.plugin;
    j["image"] = s.image ? Json(s.image->string()) : Json(nullptr);
    j["stops"] = s.stops;
    Json devices = Json::array();
    for (const DeviceInfo& d : s.devices) {
        Json zones = Json::array();
        for (const ZoneInfo& z : d.zones) zones.push_back(Json{{"name", z.name}, {"leds", z.leds}});
        devices.push_back(Json{{"name", d.name}, {"kind", d.kind}, {"zones", zones}});
    }
    j["devices"] = devices;
    j["last_sent"] = s.last_sent ? Json(*s.last_sent) : Json(nullptr);
    j["error"] = s.error ? Json(*s.error) : Json(nullptr);
    return j;
}

Status status_from(const Json& j) {
    Status s;
    s.version = j.at("version").get<std::string>();
    s.paused = j.at("paused").get<bool>();
    std::string mode = j.at("mode").get<std::string>();
    if (mode != "arc" && mode != "single") throw Error(std::format("unknown variant `{}`", mode));
    s.mode = (mode == "arc") ? Mode::Arc : Mode::Single;
    s.plugin = j.at("plugin").get<std::string>();
    if (j.contains("image") && !j["image"].is_null()) s.image = j["image"].get<std::string>();
    s.stops = j.at("stops").get<std::vector<std::string>>();
    for (const Json& d : j.at("devices")) {
        DeviceInfo device{d.at("name").get<std::string>(), d.at("kind").get<int32_t>(), {}};
        for (const Json& z : d.at("zones")) {
            device.zones.push_back(ZoneInfo{z.at("name").get<std::string>(), z.at("leds").get<uint32_t>()});
        }
        s.devices.push_back(std::move(device));
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

std::string to_json(Request request) {
    Json j;
    j["cmd"] = REQUEST_NAMES[static_cast<int>(request)];
    return j.dump();
}

std::string to_json(const Response& response) {
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
    return j.dump();  // throws on invalid UTF-8, like serde_json would refuse it
}

Request request_from_json(std::string_view line) {
    Json j = parse(line);
    if (!j.is_object()) throw Error("invalid type: expected internally tagged enum Request");
    if (!j.contains("cmd")) throw Error("missing field `cmd`");
    if (!j["cmd"].is_string()) throw Error("invalid type: expected variant identifier");
    std::string cmd = j["cmd"].get<std::string>();
    for (int i = 0; i < 4; ++i) {
        if (cmd == REQUEST_NAMES[i]) return static_cast<Request>(i);
    }
    throw Error(std::format("unknown variant `{}`, expected one of `status`, `pause`, `resume`, `apply`", cmd));
}

Response response_from_json(std::string_view line) {
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

std::filesystem::path socket_path() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime && std::filesystem::path(runtime).is_absolute()) {
        return std::filesystem::path(runtime) / "wallpapercolor.sock";
    }
    const char* user = std::getenv("USER");
    return std::format("/tmp/wallpapercolor-{}.sock", user ? user : "user");
}

Response call(Request request) {
    Fd socket = unix_connect(socket_path());
    set_timeouts(socket, Seconds(3), Seconds(3));
    write_all(socket, to_json(request) + "\n");
    return response_from_json(read_line(socket));
}

}  // namespace wpc::ipc
