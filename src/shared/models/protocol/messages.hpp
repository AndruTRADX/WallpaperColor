// Model: the messages between the terminal and the service. Both programs must agree on them,
// so they live here once; the protocol_json service turns them into JSON lines, and each program
// has its own side of the socket. Settings are not part of it: the terminal edits the config
// file and the service reloads it.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "drivers/sdk/hardware.hpp"
#include "shared/domain/colours/extract.hpp"

namespace wpc::ipc {

enum class Request {
    Status,
    /// Stop sending colours until Resume (in memory only; a restart resumes).
    Pause,
    Resume,
    /// Send the current wallpaper's colours again now.
    Apply,
    /// Paint everything white (calibration applied) to compare devices, until TestOff.
    Test,
    TestOff,
};

struct ZoneInfo {
    std::string name;
    uint32_t leds = 0;
    /// What it is, after [[component]] rules.
    Component component = Component::Other;
    Shape shape = Shape::Ring;
    bool operator==(const ZoneInfo&) const = default;
};

struct DeviceInfo {
    std::string name;
    /// The driver it comes from.
    std::string driver;
    std::vector<ZoneInfo> zones;
    bool operator==(const DeviceInfo&) const = default;
};

/// An installed driver module and what the service does with it.
struct DriverState {
    std::string name;
    /// What it can paint.
    std::vector<Component> components;
    /// In memory now (one of its components is chosen).
    bool loaded = false;
    /// Its last problem, cleared by the next success.
    std::optional<std::string> error;
    bool operator==(const DriverState&) const = default;
};

struct Status {
    std::string version;
    bool paused = false;
    /// Everything is white for comparing devices (see Request::Test).
    bool testing = false;
    Mode mode = Mode::Arc;
    /// Wallpaper plugin (org.kde.image, org.kde.slideshow…).
    std::string plugin;
    /// Picture the colours come from.
    std::optional<std::filesystem::path> image;
    /// Gradient stops last computed, as #rrggbb.
    std::vector<std::string> stops;
    /// Devices of the last send, every zone included (chosen or not).
    std::vector<DeviceInfo> devices;
    /// Every installed driver module.
    std::vector<DriverState> drivers;
    /// Seconds since the Unix epoch of the last successful send.
    std::optional<uint64_t> last_sent;
    /// Last problem (OpenLinkHub or Plasma unreachable…), cleared by the next success.
    std::optional<std::string> error;
    bool operator==(const Status&) const = default;
};

/// One answer: exactly one of these, by `kind`.
struct Response {
    enum class Kind { Status, Ok, Error };
    Kind kind = Kind::Ok;
    Status status;        // when kind == Status
    std::string message;  // when kind == Error

    static Response ok() { return Response{Kind::Ok, {}, {}}; }
    static Response error(std::string message) { return Response{Kind::Error, {}, std::move(message)}; }
    static Response of(Status status) { return Response{Kind::Status, std::move(status), {}}; }
    bool operator==(const Response&) const = default;
};

}  // namespace wpc::ipc
