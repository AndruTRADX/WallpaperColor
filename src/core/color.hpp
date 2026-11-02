// 8-bit RGB colour with HSV conversions and hex formatting.
//
// All colour maths uses `float` (Rust f32) so results match the Rust version bit for bit.
// Write float literals with an `f` (0.3f): a plain 0.3 is a double and changes the rounding.

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

/// One byte from two hex characters, the way Rust's u8::from_str_radix(.., 16) reads them:
/// two hex digits, or (a Rust quirk we keep) a '+' followed by one hex digit.
std::optional<uint8_t> parse_hex_byte(char a, char b);

// Small maths helpers that behave exactly like their Rust namesakes.

/// Rust's `f32::rem_euclid`: the remainder, always >= 0 (for b > 0).
float rem_euclid(float a, float b);

/// Rust's `f32::total_cmp(a, b) == Less`: like a < b, but -0.0 < +0.0 and NaN sorts last.
bool total_less(float a, float b);

}  // namespace wpc
