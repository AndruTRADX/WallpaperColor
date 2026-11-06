// Feature: paint the wallpaper's colours. Asks the desktop what is on screen, turns a wallpaper
// package into its picture for the colour scheme; if the picture or the settings changed (or
// `force`), reads it, takes its colours and paints them.
//
// Also what every painting does (the white test paints too): find the devices, decide each
// zone's colours, fade to them, record the outcome for `status`, or schedule a retry.

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "daemon/application/contracts/clock.hpp"
#include "daemon/application/contracts/colour_scheme.hpp"
#include "daemon/application/contracts/desktop_wallpaper.hpp"
#include "daemon/application/contracts/external/driver_modules.hpp"
#include "daemon/application/contracts/external/pictures.hpp"
#include "daemon/application/contracts/memory.hpp"
#include "daemon/application/contracts/wallpaper_packages.hpp"
#include "daemon/application/models/service_state.hpp"

namespace wpc {

/// `showing` hears which picture is on screen before it is read.
void paint_wallpaper(ServiceState& state, DesktopWallpaper& desktop, WallpaperPackages& packages, ColourScheme& scheme,
                     Pictures& pictures, DriverModules& drivers, Clock& clock, Memory& memory, bool force,
                     const std::function<void(const std::filesystem::path&)>& showing = {});

/// The computer woke from sleep: what the devices show is not known, so the next fade starts
/// from what they report.
void hardware_woke(ServiceState& state);

/// Paints `stops` in `mode` on the loaded drivers' chosen zones, with the settings in `state`,
/// fading from what they show; records the outcome in state.shown (devices, last problem,
/// time). A problem schedules a retry.
void paint_colours(ServiceState& state, DriverModules& drivers, Clock& clock, const std::vector<Rgb>& stops, Mode mode);

/// Schedules a retry (2 s, 4 s … 60 s); logs the problem when it is a new one.
void schedule_retry(ServiceState& state, Clock& clock, const std::string& problem);

/// How long until the pending retry (zero if it is due); nullopt when there is none.
std::optional<TimePoint::duration> time_to_retry(const ServiceState& state, Clock& clock);

}  // namespace wpc
