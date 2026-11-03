// Event: the settings may have changed (their file was written; its folder was created).

#pragma once

#include "daemon/application/contracts/external/settings_source.hpp"
#include "daemon/application/contracts/file_watch.hpp"
#include "daemon/application/models/notifications.hpp"

namespace wpc {

class SettingsChangedEvent {
public:
    SettingsChangedEvent(SettingsSource& settings, FileWatch& files, Notify notify)
        : settings_(settings), files_(files), notify_(std::move(notify)) {}

    /// Starts listening. Throws wpc::Error when the folder can't be watched.
    void start();

private:
    void watch_folder();

    SettingsSource& settings_;
    FileWatch& files_;
    Notify notify_;
    bool watching_folder_ = false;
};

}  // namespace wpc
