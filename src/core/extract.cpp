#include "core/extract.hpp"

#include <algorithm>
#include <cmath>

namespace wpc {

namespace {

using Pixel = std::array<uint8_t, 3>;

/// Skin with less chroma than this (beige, greyish peach) is treated as grey.
constexpr float PALE_SKIN_CHROMA = 0.15f;

/// Soft orange, light or in shadow: where anime and photo skin falls (vivid orange is not skin).
bool is_skin(Hsv hsv) {
    return hsv.h >= 5.0f && hsv.h <= 45.0f && hsv.s >= 0.12f && hsv.s <= 0.6f && hsv.v >= 0.2f;
}

float chroma(Hsv hsv) {
    return hsv.s * hsv.v;
}

/// Saturation for the LEDs: dark colours raised to `min_saturation`, light ones kept pastel.
float led_saturation(Hsv hsv, const ExtractConfig& config) {
    float floor = static_cast<float>(config.min_saturation);
    float dark = std::max(hsv.s, floor);
    float light = std::clamp(hsv.s * static_cast<float>(config.pastel_gain), hsv.s,
                             std::max(floor, hsv.s));
    float t = std::clamp((hsv.v - 0.55f) / (0.8f - 0.55f), 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);  // smoothstep
    return dark + (light - dark) * t;
}

float vividness(Hsv hsv, float share, const ExtractConfig& config) {
    float score = share * hsv.s * hsv.s * (0.3f + hsv.v);
    return is_skin(hsv) ? score * static_cast<float>(config.skin_weight) : score;
}

/// The LED version of a palette colour: full brightness, adjusted saturation.
Rgb led_colour(Hsv hsv, const ExtractConfig& config) {
    return Rgb::from_hsv(Hsv{hsv.h, led_saturation(hsv, config), 1.0f});
}

/// The channel with the widest range in `pixels`, with that range.
struct Widest {
    int channel = 0;
    uint8_t lo = 0;
    uint8_t hi = 0;
};

Widest widest_channel(const std::vector<Pixel>& pixels) {
    Pixel lo = {255, 255, 255};
    Pixel hi = {0, 0, 0};
    for (const Pixel& p : pixels) {
        for (int c = 0; c < 3; ++c) {
            lo[c] = std::min(lo[c], p[c]);
            hi[c] = std::max(hi[c], p[c]);
        }
    }
    // On a tie the *last* channel wins (Rust's max_by_key), hence `>=`.
    Widest best{0, lo[0], hi[0]};
    int best_range = -1;
    for (int c = 0; c < 3; ++c) {
        int range = std::max(0, hi[c] - lo[c]);
        if (range >= best_range) {
            best = Widest{c, lo[c], hi[c]};
            best_range = range;
        }
    }
    return best;
}

struct PaletteCount {
    Rgb color;
    size_t count = 0;
};

/// Range cut: split the box with the widest channel range at the middle of that range until
/// there are `n` boxes; each box becomes its average colour. Largest box first.
std::vector<PaletteCount> range_cut(std::vector<Pixel> pixels, size_t n) {
    std::vector<std::vector<Pixel>> boxes;
    boxes.push_back(std::move(pixels));

    while (boxes.size() < n) {
        // The box whose widest channel has the largest range (the last one on a tie).
        bool found = false;
        size_t best_box = 0;
        Widest best;
        for (size_t i = 0; i < boxes.size(); ++i) {
            Widest w = widest_channel(boxes[i]);
            if (w.hi <= w.lo) continue;  // a single colour: cannot be cut
            if (!found || w.hi - w.lo >= best.hi - best.lo) {
                found = true;
                best_box = i;
                best = w;
            }
        }
        if (!found) break;  // every box is a single colour
        uint8_t mid = static_cast<uint8_t>(best.lo + (best.hi - best.lo) / 2);

        // Take the box out the way Rust's swap_remove does (the last box moves into its
        // place): the order of the boxes decides which of two equal boxes is listed first.
        std::vector<Pixel> box = std::move(boxes[best_box]);
        boxes[best_box] = std::move(boxes.back());
        boxes.pop_back();

        std::vector<Pixel> low;
        std::vector<Pixel> high;
        for (const Pixel& p : box) {
            (p[best.channel] <= mid ? low : high).push_back(p);
        }
        boxes.push_back(std::move(low));
        boxes.push_back(std::move(high));
    }

    std::vector<PaletteCount> out;
    for (const std::vector<Pixel>& box : boxes) {
        if (box.empty()) continue;
        uint64_t count = box.size();
        uint64_t sum[3] = {0, 0, 0};
        for (const Pixel& p : box) {
            for (int c = 0; c < 3; ++c) sum[c] += p[c];
        }
        auto average = [&](int c) { return static_cast<uint8_t>((sum[c] + count / 2) / count); };
        out.push_back(PaletteCount{Rgb{average(0), average(1), average(2)}, box.size()});
    }
    // Stable: equal boxes keep their order.
    std::stable_sort(out.begin(), out.end(),
                     [](const PaletteCount& a, const PaletteCount& b) { return a.count > b.count; });
    return out;
}

struct Candidate {
    Rgb color;
    Hsv hsv;
    float share = 0;
};

}  // namespace

std::vector<Rgb> Extraction::stops(Mode mode) const {
    if (mode == Mode::Arc) return {arc.begin(), arc.end()};
    return {led};
}

float hue_distance(float a, float b) {
    float d = rem_euclid(a - b, 360.0f);
    return std::min(d, 360.0f - d);
}

Extraction extract(const Image& image, const ExtractConfig& config) {
    uint32_t side = std::max<uint32_t>(config.sample_size, 1);
    std::vector<Pixel> pixels = resize_to_rgb8(image, side, side);

    Extraction result;
    float total = static_cast<float>(pixels.size());
    for (const PaletteCount& entry : range_cut(std::move(pixels), std::max<size_t>(config.palette_size, 1))) {
        result.palette.push_back(PaletteEntry{entry.color, static_cast<float>(entry.count) / total});
    }

    // Palette entries that may be picked: not specks, not grey, not pale skin.
    std::vector<Candidate> candidates;
    for (const PaletteEntry& entry : result.palette) {
        Hsv hsv = entry.color.to_hsv();
        bool big_enough = entry.share >= static_cast<float>(config.min_share);
        bool colourful = chroma(hsv) >= static_cast<float>(config.min_chroma);
        bool pale_skin = is_skin(hsv) && chroma(hsv) < PALE_SKIN_CHROMA;
        if (big_enough && colourful && !pale_skin) {
            candidates.push_back(Candidate{entry.color, hsv, entry.share});
        }
    }
    // Most vivid first (stable: equally vivid entries keep the palette order).
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](const Candidate& a, const Candidate& b) {
                         return total_less(vividness(b.hsv, b.share, config),
                                           vividness(a.hsv, a.share, config));
                     });

    if (candidates.empty()) {  // a colourless picture: white
        result.picked = std::nullopt;
        result.led = WHITE;
        result.arc = {WHITE, WHITE, WHITE};
        return result;
    }
    const Candidate& main = candidates[0];

    // Accents: rank the rest by colourfulness (chroma², skin discounted) over √share, so a
    // small purple sparkle beats a large dull slate background.
    struct Accent {
        Hsv hsv;
        float score = 0;
    };
    std::vector<Accent> accents;
    for (size_t i = 1; i < candidates.size(); ++i) {
        const Candidate& c = candidates[i];
        float skin = is_skin(c.hsv) ? static_cast<float>(config.skin_weight) : 1.0f;
        float score = chroma(c.hsv) * chroma(c.hsv) * std::sqrt(c.share) * skin;
        accents.push_back(Accent{c.hsv, score});
    }
    std::stable_sort(accents.begin(), accents.end(), [](const Accent& a, const Accent& b) {
        return total_less(b.score, a.score);
    });

    // The main colour, then accents whose hue is far enough from every hue already taken.
    std::vector<Hsv> hues = {main.hsv};
    for (const Accent& accent : accents) {
        if (hues.size() == 3) break;
        bool distinct = std::all_of(hues.begin(), hues.end(), [&](const Hsv& h) {
            return hue_distance(h.h, accent.hsv.h) >= ARC_MIN_HUE_GAP;
        });
        if (distinct) hues.push_back(accent.hsv);
    }

    Rgb led = led_colour(main.hsv, config);
    std::vector<Rgb> arc;
    for (const Hsv& hsv : hues) arc.push_back(led_colour(hsv, config));
    if (arc.size() < 3) {
        // a lighter shade of the main colour (paler than a pastel main colour too)
        arc.push_back(Rgb::from_hsv(Hsv{main.hsv.h, std::min(led.to_hsv().s * 0.5f, 0.35f), 1.0f}));
    }
    if (arc.size() < 3) {
        // and a dimmer one: deep for vivid colours, only slightly dimmer for pastels
        Hsv led_hsv = led.to_hsv();
        bool pastel = led_hsv.s < static_cast<float>(config.min_saturation) * 0.9f;
        float v = pastel ? 0.7f : 0.45f;
        arc.push_back(Rgb::from_hsv(Hsv{led_hsv.h, led_hsv.s, v}));
    }

    result.picked = main.color;
    result.led = led;
    result.arc = {arc[0], arc[1], arc[2]};
    return result;
}

Extraction extract_path(const std::filesystem::path& path, const ExtractConfig& config) {
    return extract(load_image(path), config);
}

}  // namespace wpc
