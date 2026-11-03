// 8-bit RGB colour with HSV conversions and hex formatting.
//
// Colour maths uses `float`. Write float literals with an `f` (0.3f): a plain 0.3 is a
// double and would turn the whole expression into double maths.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace wpc {

/// Hue in degrees [0, 360), saturation and value in [0, 1].
struct Hsv {
    float h = 0;
    float s = 0;
    float v = 0;
};

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    Hsv to_hsv() const;
    static Rgb from_hsv(Hsv hsv);

    /// "#rrggbb"
    std::string hex() const;

    /// Parses "#rrggbb" or "rrggbb"; nullopt if it is not a colour.
    static std::optional<Rgb> parse(std::string_view text);

    bool operator==(const Rgb&) const = default;
};

inline constexpr Rgb WHITE{255, 255, 255};

/// The error message for a text that `Rgb::parse` rejects.
std::string invalid_colour_message(std::string_view text);

/// One byte from two hex digits ("7f" → 127); nullopt if they are not hex digits.
std::optional<uint8_t> parse_hex_byte(char a, char b);

/// The remainder of a / b that is never negative (for b > 0): rem_euclid(-30, 360) == 330.
/// std::fmod would give -30.
float rem_euclid(float a, float b);

}  // namespace wpc
