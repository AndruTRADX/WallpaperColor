// Model: what the service remembers between events. The features read and change it; the
// service loop owns it.

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "daemon/application/contracts/clock.hpp"
#include "daemon/application/models/driver_module.hpp"
#include "daemon/domain/retry.hpp"
#include "shared/domain/settings/settings.hpp"
#include "shared/models/protocol/messages.hpp"

namespace wpc {

/// What the service knows about the drivers.
struct DriversState {
    /// Every installed driver (from the last time the settings were applied).
    std::vector<DriverModule> installed;
    /// The loaded drivers, with the settings each was created with: a change creates it again.
    std::map<std::string, DriverSettings> loaded;
    /// The last problem of each driver that has one.
    std::map<std::string, std::string> errors;
    /// What each output shows now (what was sent last), by Output::key: where fades start.
    std::map<std::string, std::vector<Rgb>> shown;
};

struct ServiceState {
    Config config;
    DriversState drivers;

    /// The slideshow folders of the wallpaper on screen (empty for other plugins).
    std::vector<std::filesystem::path> slide_folders;

    /// What was last painted: picture, its mtime and the settings used.
    std::optional<std::string> last_painted;
    /// The colours last taken from a picture, and that picture's key (as last_painted): a retry
    /// or a repeat doesn't read it again.
    std::string extracted_key;
    std::vector<Rgb> extracted_stops;

    /// A pending retry: when, and how long the last wait was.
    struct Retry {
        TimePoint at;
        RetryDelay delay;
    };
    std::optional<Retry> retry;

    bool paused = false;
    /// White test: everything white, the wallpaper ignored.
    bool testing = false;

    /// What `status` shows: the picture, its colours, the devices, the last problem.
    ipc::Status shown;
};

}  // namespace wpc
