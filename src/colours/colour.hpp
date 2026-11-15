// Colour maths: HSV, hex text, gradients. Pure functions over the SDK's plain Rgb.
//
// Colour maths uses `float`. Write float literals with an `f` (0.3f): a plain 0.3 is a
// double and would turn the whole expression into double maths.
//
// Gradients interpolate through HSV along the shorter way round the hue circle, so blue to
// orange passes through vivid colours instead of the grey an RGB blend would give.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "drivers/sdk/hardware.hpp"

namespace wpc {

/// Hue in degrees [0, 360), saturation and value in [0, 1].
struct Hsv {
    float h = 0;
    float s = 0;
    float v = 0;
};

Hsv to_hsv(Rgb c);
/// The colour of `hsv`; any hue is taken round the circle, and saturation and value outside
/// 0-1 (or not numbers) are taken as the nearest valid colour.
Rgb to_rgb(Hsv hsv);

/// "#rrggbb"
std::string to_hex(Rgb c);

/// Parses "#rrggbb" or "rrggbb"; nullopt if it is not a colour.
std::optional<Rgb> parse_colour(std::string_view text);

/// The error message for a text that parse_colour() rejects.
std::string invalid_colour_message(std::string_view text);

/// One byte from two hex digits ("7f" → 127); nullopt if they are not hex digits.
std::optional<uint8_t> parse_hex_byte(char a, char b);

/// The remainder of a / b that is never negative (for b > 0): rem_euclid(-30, 360) == 330.
/// std::fmod would give -30.
float rem_euclid(float a, float b);

/// `n` colours going through `stops` in order. `closed` (a ring) wraps back to the first stop so
/// there is no jump where the ring closes; otherwise the first and last LED get the end stops.
std::vector<Rgb> gradient(const std::vector<Rgb>& stops, size_t n, bool closed);

/// HSV blend of `a` and `b` (`t` = 0 gives `a`). A colourless end takes the other end's hue.
Rgb mix(Rgb a, Rgb b, float t);

}  // namespace wpc
