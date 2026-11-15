// The service's rules, as pure functions (no ports, no I/O: a test needs no fake):
// - which zone of which device gets which colours (outputs);
// - how a fade goes: how many frames, where it starts, each frame;
// - how long to wait before trying again after a problem;
// - which drivers the chosen components need;
// - which file names are pictures.

#pragma once

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "colours/paint.hpp"
#include "drivers/sdk/hardware.hpp"
#include "protocol/messages.hpp"
#include "settings/settings.hpp"

namespace wpc {

// ---------------------------------------------------------------------------------------------
// Outputs: what each device gets, from the devices a driver found, the chosen components and
// the colours.

/// The chosen zones of one device and their new colours.
struct Output {
    std::string driver;
    /// The device's number in the driver's devices().
    size_t device = 0;
    /// Fades remember what an output shows under this name.
    std::string key;
    /// Zone number and LED count of every chosen zone, in order.
    std::vector<std::pair<size_t, size_t>> zones;
    /// The new colours, one per LED of those zones.
    std::vector<Rgb> target;
    /// What the device says those LEDs show now; empty if it can't tell.
    std::vector<Rgb> reported;
};

/// Adds the outputs of `driver`'s `devices` to `outputs`, and every device (all zones, chosen
/// or not, with what each zone is) to `found`.
void plan_outputs(const std::string& driver, const std::vector<Device>& devices, const ComponentSettings& components,
                  Painter& painter, std::vector<Output>& outputs, std::vector<ipc::DeviceInfo>& found);

// ---------------------------------------------------------------------------------------------
// Fades: from what LEDs show to new colours.

/// Frames per second of a fade.
inline constexpr int FADE_FPS = 25;

/// How many frames a fade of `seconds` takes (at most ranges::FADE.max); 1 when nothing changes.
int fade_frames(double seconds, bool changes);

/// Where a fade starts: what was sent last (`last_sent`, if any) when it has the right size,
/// else what the device reports showing when it has, else the target itself (no fade).
const std::vector<Rgb>& fade_start(const std::vector<Rgb>* last_sent, const std::vector<Rgb>& reported,
                                   const std::vector<Rgb>& target);

/// Frame `frame` (1 … `frames`) between `from` and `to`, eased in and out. The last one is `to`.
std::vector<Rgb> fade_frame(const std::vector<Rgb>& from, const std::vector<Rgb>& to, int frame, int frames);

/// fade_frame() written over `out` (the size of `to`, `from` too), so a fade reuses one buffer
/// for all its frames. True if `out` changed: false means it already held this frame.
bool fade_frame_into(std::span<const Rgb> from, std::span<const Rgb> to, int frame, int frames, std::span<Rgb> out);

// ---------------------------------------------------------------------------------------------
// Retries: 2 s, then twice as long each time, up to 60 s.

using RetryDelay = std::chrono::steady_clock::duration;

/// The wait after `last` (nullopt: the first problem).
RetryDelay next_retry_delay(std::optional<RetryDelay> last);

// ---------------------------------------------------------------------------------------------
// Drivers and pictures

/// Whether a driver that paints `paints` is needed: it paints at least one chosen component.
bool driver_needed(const std::vector<Component>& paints, const ComponentSettings& chosen);

/// The chosen components that no installed driver paints (`installed`: what each one paints).
std::vector<Component> unpainted(const std::vector<std::vector<Component>>& installed, const ComponentSettings& chosen);

/// Whether `name` is a picture a wallpaper can be (by extension, any case).
bool is_picture_name(std::string_view name);

}  // namespace wpc
