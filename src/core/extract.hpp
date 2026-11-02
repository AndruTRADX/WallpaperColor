// Picks LED-friendly colours from a wallpaper: one main colour, plus three for an arc.
//
// 1. Downscale the picture (only the overall colours matter, and it keeps this fast).
// 2. Reduce it to a small palette by repeatedly cutting the colour box with the widest range
//    at the middle of that range (not at the median pixel: in a mostly-black picture that only
//    slices the black into identical pieces and averages small vivid areas away).
// 3. Ignore specks (entries covering less than `min_share` of the picture) and near-grey
//    entries (chroma below `min_chroma`). Chroma, the gap between the highest and lowest
//    channel, is used instead of saturation because saturation blows up on very dark colours:
//    an almost-black #1b191e has 17 % saturation but only 2 % chroma.
// 4. Pick the most *vivid* remaining entry: share × saturation² × (0.3 + value), so a bright
//    character on a grey background wins over the background. Skin tones (soft orange,
//    shadows included) count only `skin_weight` as much, and pale skin counts as grey:
//    character art is full of skin, which is rarely its colour.
// 5. Push it to full brightness. Dark colours (shadows of something vivid) are raised to at
//    least `min_saturation`, or they look washed out; light, barely saturated colours are
//    pastel and stay pastel (saturation × `pastel_gain`, below `min_saturation`), with a
//    smooth blend in between. A fixed floor turned soft peach into orange.
//    A picture with no real colour (black and white) gives white.
// 6. For the arc, keep taking the next most vivid entries whose hue is at least
//    ARC_MIN_HUE_GAP away from the ones already taken; when the picture has fewer distinct
//    colours, fill in with a lighter and a dimmer shade of the main colour.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include "core/color.hpp"
#include "core/image.hpp"

namespace wpc {

/// The `[extract]` settings.
struct ExtractConfig {
    /// Palette size used to find the main colours.
    size_t palette_size = 10;
    /// The picture is downscaled to this many pixels per side before analysis.
    uint32_t sample_size = 96;
    /// Saturation the LED colour is raised to (0-1).
    double min_saturation = 0.7;
    /// Palette entries with less chroma than this (max - min channel, 0-1) count as grey.
    /// When every entry is grey the picture is colourless and the LEDs go white.
    double min_chroma = 0.06;
    /// Palette entries covering less of the picture than this (0-1) are ignored as specks.
    double min_share = 0.005;
    /// How much a skin tone counts when picking the colour (1 = like any other, 0 = never).
    double skin_weight = 0.15;
    /// Saturation multiplier for light colours (pastels), capped at `min_saturation`.
    double pastel_gain = 2.5;

    bool operator==(const ExtractConfig&) const = default;
};

/// The `[led] mode` setting.
enum class Mode {
    /// Three colours from the picture as a gradient: around each ring, top to bottom on RAM.
    Arc,
    /// The main colour on every LED.
    Single,
};

/// One palette entry: a colour and the share of the picture it covers (0-1).
struct PaletteEntry {
    Rgb color;
    float share = 0;
};

/// Result of analysing one picture.
struct Extraction {
    /// Main colours, most common first; the shares sum to 1.
    std::vector<PaletteEntry> palette;
    /// Palette entry chosen as the most vivid, before the LED adjustment
    /// (nullopt: colourless picture).
    std::optional<Rgb> picked;
    /// Colour to send to the LEDs.
    Rgb led;
    /// Three LED colours for an arc; arc[0] == led.
    std::array<Rgb, 3> arc;

    /// Gradient stops for `mode`: the three arc colours, or just the main colour.
    std::vector<Rgb> stops(Mode mode) const;
};

/// Minimum hue difference (degrees) between two arc colours.
inline constexpr float ARC_MIN_HUE_GAP = 30.0f;

/// Analyses a decoded picture.
Extraction extract(const Image& image, const ExtractConfig& config);

/// Reads the picture (by content, not extension) and analyses it. Throws wpc::Error.
Extraction extract_path(const std::filesystem::path& path, const ExtractConfig& config);

/// Shortest distance between two hues, in degrees (0-180).
float hue_distance(float a, float b);

}  // namespace wpc
