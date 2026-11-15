// What the service does: apply the settings, paint the wallpaper (or white, for the white test),
// pause and resume, answer the terminal's requests. Each use case is a function over the
// ServiceState and only the ports it uses, so its signature says what it touches and a test
// fakes only that. The service loop (loop.hpp) decides when each one runs.

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "daemon/service/ports.hpp"
#include "daemon/service/state.hpp"
#include "drivers/host/driver_host.hpp"
#include "pictures/picture_files.hpp"
#include "protocol/messages.hpp"

namespace wpc {

/// One implementation of each port, as main.cpp wires them.
struct Ports {
    Desktop& desktop;
    PictureReader& pictures;
    SettingsSource& settings;
    DriverHost& drivers;
    Clock& clock;
};

/// Reads the settings (the defaults when they have a mistake), then loads the drivers the chosen
/// components need and unloads the others. A loaded driver whose table didn't change is kept,
/// with its connection. `first`: when the service starts (only the log line differs).
void apply_settings(ServiceState& state, SettingsSource& settings, DriverHost& drivers, bool first);

/// Asks the desktop what is on screen; if the picture or the settings changed (or `force`),
/// reads it, takes its colours and paints them. `reading` hears which picture is about to be
/// read. A problem is shown in `status` and tried again later (schedule_retry).
void paint_wallpaper(ServiceState& state, Ports& ports, bool force,
                     const std::function<void(const std::filesystem::path&)>& reading = {});

/// Paints `stops` in `mode` on the loaded drivers' chosen zones, with the settings in `state`,
/// fading from what they show; records the outcome in state.shown (devices, last problem,
/// time). A driver that fails is left out of the rest of the fade and tried again later; the
/// others are painted.
void paint_colours(ServiceState& state, DriverHost& drivers, Clock& clock, const std::vector<Rgb>& stops, Mode mode);

/// The computer woke from sleep: what the devices show is not known, so the next fade starts
/// from what they report.
void hardware_woke(ServiceState& state);

/// The white test: everything white, through the calibrations, so devices can be compared
/// while adjusting them; the wallpaper is ignored until it ends.
void start_white_test(ServiceState& state);
void stop_white_test(ServiceState& state);
void paint_white(ServiceState& state, DriverHost& drivers, Clock& clock);

/// While paused, colours are taken but not painted; the devices keep the last ones. Only in
/// memory: a restart resumes.
void pause(ServiceState& state);
void resume(ServiceState& state);

/// Schedules a retry (2 s, 4 s … 60 s); logs the problem when it is a new one.
void schedule_retry(ServiceState& state, Clock& clock, const std::string& problem);

/// How long until the pending retry (zero if it is due); nullopt when there is none.
std::optional<TimePoint::duration> time_to_retry(const ServiceState& state, Clock& clock);

/// What `wallpapercolor status` and the terminal UI show.
ipc::Status report_status(const ServiceState& state);

/// Runs the use case of `request`. `paint_now` is set when the wallpaper must be painted again
/// at once (the request changed what is shown).
ipc::Response run_request(ipc::Request request, ServiceState& state, bool& paint_now);

/// The answer line to a request line from the terminal (a line that is not a request gets an
/// error answer). Sets `paint_now` as run_request() does.
std::string answer_line(const std::string& line, ServiceState& state, bool& paint_now);

}  // namespace wpc
