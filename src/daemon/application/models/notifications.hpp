// Model: what the events send, a notification that something happened. The service loop receives
// them and runs the features.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <variant>

#include "utils/channel.hpp"

namespace wpc {

/// The wallpaper on screen may have changed.
struct DesktopChangedNotice {};
/// The settings may have changed.
struct SettingsChangedNotice {};
/// The terminal sent a request line; the answer line goes to `reply`.
struct RequestNotice {
    std::string line;
    std::shared_ptr<Channel<std::string>> reply;
};

using Notification = std::variant<DesktopChangedNotice, SettingsChangedNotice, RequestNotice>;
using Notify = std::function<void(Notification)>;
/// Where the notifications wait for the service loop.
using Notifications = Channel<Notification>;

}  // namespace wpc
