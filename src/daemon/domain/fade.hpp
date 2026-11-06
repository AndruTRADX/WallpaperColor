// Fading LEDs from what they show to new colours: how many frames, and what each frame is.
// Pure maths; showing the frames is the application's job.

#pragma once

#include <span>
#include <vector>

#include "shared/domain/colours/color.hpp"

namespace wpc {

/// Frames per second of a fade.
inline constexpr int FADE_FPS = 25;
/// The longest fade: requests from the terminal wait while one runs, and give up after 2 s.
inline constexpr double FADE_MAX_SECONDS = 1.5;

/// How many frames a fade of `seconds` takes (at most FADE_MAX_SECONDS); 1 when nothing changes.
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

}  // namespace wpc
