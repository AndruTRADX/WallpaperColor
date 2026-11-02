// Gradients that spread a few colours over a row or ring of LEDs.
//
// Interpolation goes through HSV along the shorter way round the hue circle, so blue to orange
// passes through vivid colours instead of the grey an RGB blend would give.

#pragma once

#include <cstddef>
#include <vector>

#include "core/color.hpp"

namespace wpc {

/// `n` colours going through `stops` in order. `closed` (a ring) wraps back to the first stop so
/// there is no jump where the ring closes; otherwise the first and last LED get the end stops.
std::vector<Rgb> gradient(const std::vector<Rgb>& stops, size_t n, bool closed);

/// HSV blend of `a` and `b` (`t` = 0 gives `a`). A colourless end takes the other end's hue.
Rgb mix(Rgb a, Rgb b, float t);

}  // namespace wpc
