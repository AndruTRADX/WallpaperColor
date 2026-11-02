// Protocol between the service and its clients (the terminal UI and commands).
//
// A Unix socket at $XDG_RUNTIME_DIR/wallpapercolor.sock; one request per connection: the
// client writes a JSON Request line, the service answers with a JSON Response line.
// Settings are not part of it: clients edit the config file and the service reloads it.
//
// The JSON is byte-for-byte what the Rust version (serde) wrote: same field names and order,
// `null` for missing optional values. See tests/golden/ipc.txt.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/extract.hpp"

namespace wpc::ipc {

enum class Request {
    Status,
    /// Stop sending colours until Resume (in memory only; a restart resumes).
    Pause,
    Resume,
    /// Send the current wallpaper's colours again now.
    Apply,
};

struct ZoneInfo {
    std::string name;
    uint32_t leds = 0;
    bool operator==(const ZoneInfo&) const = default;
};

struct DeviceInfo {
    std::string name;
    /// OpenRGB device type (1 = RAM).
    int32_t kind = 0;
    std::vector<ZoneInfo> zones;
    bool operator==(const DeviceInfo&) const = default;
};

struct Status {
    std::string version;
    bool paused = false;
    Mode mode = Mode::Arc;
    /// Wallpaper plugin (org.kde.image, org.kde.slideshow…).
    std::string plugin;
    /// Picture the colours come from.
    std::optional<std::filesystem::path> image;
    /// Gradient stops last computed, as #rrggbb.
    std::vector<std::string> stops;
    /// Devices of the last successful send.
    std::vector<DeviceInfo> devices;
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

// JSON, one line, no trailing newline. Throw wpc::Error on bad input.
std::string to_json(Request request);
std::string to_json(const Response& response);
Request request_from_json(std::string_view line);
Response response_from_json(std::string_view line);

/// $XDG_RUNTIME_DIR/wallpapercolor.sock (or /tmp/wallpapercolor-$USER.sock).
std::filesystem::path socket_path();

/// Sends one request to the running service and waits (up to 3 s) for the answer.
/// Throws wpc::Error (for example when the service is not running).
Response call(Request request);

}  // namespace wpc::ipc
