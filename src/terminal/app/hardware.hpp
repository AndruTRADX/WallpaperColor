// The terminal's use cases about the hardware and the colours: what every driver finds
// (`wallpapercolor devices`, the Drivers page), painting once (`wallpapercolor set`), the
// colours a picture gives (`wallpapercolor extract`) and the terminal UI's preview. Drivers are
// loaded in the terminal itself, asked, and unloaded again: `set` works without the service, and
// looking at hardware doesn't make the service load drivers nobody chose.

#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "colours/extract.hpp"
#include "protocol/messages.hpp"
#include "settings/settings.hpp"
#include "terminal/app/ports.hpp"

namespace wpc {

// ---------------------------------------------------------------------------------------------
// What every installed driver finds, zone by zone

struct ListedZone {
    std::string name;
    uint32_t leds = 0;
    /// What it is, after [[component]] rules.
    Component component = Component::Other;
    /// Whether it is painted.
    bool chosen = false;
};

struct ListedDevice {
    std::string name;
    /// What its first LED shows now, if the device can tell.
    std::optional<Rgb> now;
    std::vector<ListedZone> zones;
};

struct ListedDriver {
    std::string name;
    /// Its table in the settings.
    std::string config_table;
    std::vector<Component> components;
    /// Why it couldn't look, if it couldn't.
    std::optional<std::string> error;
    std::vector<ListedDevice> devices;
    /// The settings the Drivers page offers for it.
    std::vector<ModuleOption> options = {};
};

struct DeviceList {
    std::vector<ListedDriver> drivers;
    /// Modules that could not be read (another build, not a driver…).
    std::vector<std::string> problems;
};

DeviceList list_devices(DriverHost& drivers, const Config& config);

/// What the Drivers page shows next to each component: what every installed driver finds that
/// is that component.
struct Detection {
    std::vector<DriverModule> modules;
    /// For each component: what was found, like "iCUE COMMANDER Core: 4 zones".
    std::array<std::vector<std::string>, ALL_COMPONENTS.size()> found;
    /// Drivers that could not look: "openrgb: cannot reach the OpenRGB server…".
    std::vector<std::string> problems;
};

Detection detect_hardware(DriverHost& drivers, const Config& config);

// ---------------------------------------------------------------------------------------------
// Painting once

struct ColoursToSet {
    /// One to three colours ("#rrggbb"), or…
    std::vector<std::string> colours;
    /// …a picture to take them from.
    std::optional<std::filesystem::path> picture;
    /// How to spread them; nullopt: as the settings say.
    std::optional<Mode> mode;
};

struct SetResult {
    std::vector<Rgb> stops;
    /// Every device painted.
    std::vector<std::string> painted;
    /// What wasn't painted, and why ("Strip: unplugged").
    std::vector<std::string> not_painted;
};

/// Paints every chosen component once. The service paints over it at the next wallpaper
/// change. Throws wpc::Error (a bad colour, an unreadable picture, nothing painted).
SetResult set_colours(DriverHost& drivers, PictureReader& pictures, const Config& config, const ColoursToSet& what);

// ---------------------------------------------------------------------------------------------
// Colours of pictures, and the preview

struct PictureColours {
    std::filesystem::path picture;
    /// The colours, or why the picture couldn't be read.
    std::optional<Extraction> colours;
    std::string error;
};

/// The colours that would be taken from `pictures`. Nothing is painted.
std::vector<PictureColours> extract_colours(PictureReader& files, const std::vector<std::filesystem::path>& pictures,
                                           const ExtractConfig& config);

/// The colours to preview: from the picture's local analysis in `mode` when there is one, else
/// what the service last took (`service_stops`).
std::vector<Rgb> preview_colours(const std::optional<Extraction>& local, Mode mode, const std::vector<Rgb>& service_stops);

struct PreviewZone {
    ipc::ZoneInfo zone;
    /// Whether its component is painted; if not, `leds` is empty.
    bool chosen = false;
    std::vector<Rgb> leds;
    /// A calibration other than 1, 1, 1 applies to it.
    bool calibrated = false;
};

struct PreviewDevice {
    std::string name;
    std::vector<PreviewZone> zones;
};

/// What `devices` would show with `stops` and the settings (white, during the white test).
/// Computed here at once, so a change shows before the service applies it.
std::vector<PreviewDevice> preview_devices(const std::vector<ipc::DeviceInfo>& devices, const std::vector<Rgb>& stops,
                                           bool testing, const Config& config);

}  // namespace wpc
