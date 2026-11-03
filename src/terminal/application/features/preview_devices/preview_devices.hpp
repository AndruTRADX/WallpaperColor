// Feature: the terminal UI's preview: the colours of the settings being edited, and what every
// device the service found would show with them. Computed here at once, so a change shows
// before the service applies it.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "shared/domain/colours/extract.hpp"
#include "shared/domain/settings/settings.hpp"
#include "shared/models/protocol/messages.hpp"

namespace wpc {

/// The colours to show: from the picture's local analysis in `mode` when there is one, else
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
std::vector<PreviewDevice> preview_devices(const std::vector<ipc::DeviceInfo>& devices, const std::vector<Rgb>& stops,
                                           bool testing, const Config& config);

}  // namespace wpc
