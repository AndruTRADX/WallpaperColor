// User configuration: ~/.config/wallpapercolor/config.toml
//
// Every field has a default, so a missing file or a partial file is fine. Unknown keys and
// wrong types are errors (a typo should not silently do nothing).

#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/extract.hpp"

namespace wpc {

/// The `[led]` table.
struct LedConfig {
    Mode mode = Mode::Arc;
    /// Seconds a colour change takes (0 = at once). At most 1.5.
    double fade = 0.6;
    bool operator==(const LedConfig&) const = default;
};

/// The `[openrgb]` table: where to send colours (an OpenRGB SDK server).
struct OpenRgbConfig {
    /// host:port. OpenLinkHub's built-in server listens on 6743 (enableOpenRGBTargetServer);
    /// OpenRGB itself on 6742.
    std::string address = "127.0.0.1:6743";
    /// Name shown to the server for this client.
    std::string client_name = "WallpaperColor";
    bool operator==(const OpenRgbConfig&) const = default;
};

/// One `[[calibration]]` entry: per-channel gains for the LEDs of one device, or of one zone
/// of it. The first matching entry wins. Names match case-insensitively and partially
/// ("fan" matches "8-LED Series Fan").
struct Calibration {
    /// Device name as `wallpapercolor devices` shows it.
    std::string device;
    /// Zone name; nullopt = every zone of the device.
    std::optional<std::string> zone;
    /// Red, green and blue gains, 0-1 (lower the channels that are too strong).
    std::array<double, 3> gain = {1.0, 1.0, 1.0};
    bool operator==(const Calibration&) const = default;
};

/// The `[motherboard]` table: the board's own RGB (Gigabyte RGB Fusion 2).
struct MotherboardConfig {
    /// Paint it too, when there is one and we may use it.
    bool enabled = true;
    /// The colour order of what is plugged into the ARGB headers ("GRB" for most fans).
    std::string argb_order = "GRB";
    /// LEDs on ARGB header 1 and 2; 0 = unknown: the whole header gets one colour.
    std::array<uint32_t, 2> argb_leds = {0, 0};
    bool operator==(const MotherboardConfig&) const = default;
};

struct Config {
    LedConfig led;
    ExtractConfig extract;
    OpenRgbConfig openrgb;
    MotherboardConfig motherboard;
    /// Colour correction for LEDs that render colours off (e.g. weak blue: white looks yellow).
    std::vector<Calibration> calibration;

    /// Gains for `zone` of `device`: the first matching calibration, else 1, 1, 1.
    std::array<float, 3> gain(std::string_view device, std::string_view zone) const;

    /// $XDG_CONFIG_HOME/wallpapercolor/config.toml (or ~/.config/...); nullopt without HOME.
    static std::optional<std::filesystem::path> default_path();

    /// Reads the file; a missing file gives the defaults. Throws wpc::Error.
    static Config load(const std::filesystem::path& path);

    /// Parses TOML text. Throws wpc::Error (the message has no file name).
    static Config from_toml(std::string_view text);

    /// The whole config as TOML, defaults included (what `wallpapercolor config` prints).
    std::string to_toml() const;

    bool operator==(const Config&) const = default;
};

/// Throws wpc::Error if `text` is not valid TOML (any keys allowed).
void check_toml(std::string_view text);

/// A float written the way the config file has it: 0.7, 1.0 (never an exponent).
std::string toml_float(double value);

/// A TOML string: "text", with quotes, backslashes and control characters escaped.
std::string toml_string(std::string_view text);

/// "arc" / "single", as written in the config file.
std::string_view mode_name(Mode mode);

}  // namespace wpc
