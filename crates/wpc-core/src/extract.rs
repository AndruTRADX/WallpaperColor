//! Picks LED-friendly colours from a wallpaper: one main colour, plus three for an arc.
//!
//! 1. Downscale the picture (only the overall colours matter, and it keeps this fast).
//! 2. Reduce it to a small palette by repeatedly cutting the colour box with the widest range
//!    at the middle of that range (not at the median pixel: in a mostly-black picture that only
//!    slices the black into identical pieces and averages small vivid areas away).
//! 3. Ignore specks (entries covering less than `min_share` of the picture) and near-grey
//!    entries (chroma below `min_chroma`). Chroma, the gap between the highest
//!    and lowest channel, is used instead of saturation because saturation blows up on very dark
//!    colours: an almost-black `#1b191e` has 17 % saturation but only 2 % chroma.
//! 4. Pick the most *vivid* remaining entry: `share × saturation² × (0.3 + value)`, so a bright
//!    character on a grey background wins over the background. Skin tones (soft orange, shadows
//!    included) count only `skin_weight` as much, and pale skin counts as grey: character art
//!    is full of skin, which is rarely its colour.
//! 5. Push it to full brightness. Dark colours (shadows of something vivid) are raised to at
//!    least `min_saturation`, or they look washed out; light, barely saturated colours are
//!    pastel and stay pastel (saturation × `pastel_gain`, below `min_saturation`), with a
//!    smooth blend in between. A fixed floor turned soft peach into orange.
//!    A picture with no real colour (black and white) gives white.
//! 6. For the arc, keep taking the next most vivid entries whose hue is at least
//!    `ARC_MIN_HUE_GAP` away from the ones already taken; when the picture has fewer distinct
//!    colours, fill in with a lighter and a dimmer shade of the main colour.

use crate::color::{Hsv, Rgb};
use image::{DynamicImage, imageops::FilterType};
use serde::{Deserialize, Serialize};
use std::path::Path;

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct ExtractConfig {
    /// Palette size used to find the main colours.
    pub palette_size: usize,
    /// The picture is downscaled to this many pixels per side before analysis.
    pub sample_size: u32,
    /// Saturation the LED colour is raised to (0–1).
    pub min_saturation: f64,
    /// Palette entries with less chroma than this (max − min channel, 0–1) count as grey.
    /// When every entry is grey the picture is colourless and the LEDs go white.
    pub min_chroma: f64,
    /// Palette entries covering less of the picture than this (0–1) are ignored as specks.
    pub min_share: f64,
    /// How much a skin tone counts when picking the colour (1 = like any other colour, 0 = never).
    pub skin_weight: f64,
    /// Saturation multiplier for light colours (pastels), capped at `min_saturation`.
    pub pastel_gain: f64,
}

impl Default for ExtractConfig {
    fn default() -> Self {
        Self {
            palette_size: 10,
            sample_size: 96,
            min_saturation: 0.7,
            min_chroma: 0.06,
            min_share: 0.005,
            skin_weight: 0.15,
            pastel_gain: 2.5,
        }
    }
}

/// Result of analysing one picture.
#[derive(Debug, Clone, PartialEq)]
pub struct Extraction {
    /// Main colours with the share of the picture each one covers (sums to 1), most common first.
    pub palette: Vec<(Rgb, f32)>,
    /// Palette entry chosen as the most vivid, before the LED adjustment (`None`: colourless picture).
    pub picked: Option<Rgb>,
    /// Colour to send to the LEDs.
    pub led: Rgb,
    /// Three LED colours for an arc; `arc[0] == led`.
    pub arc: [Rgb; 3],
}

impl Extraction {
    /// Gradient stops for `mode`: the three arc colours, or just the main colour.
    pub fn stops(&self, mode: crate::config::Mode) -> Vec<Rgb> {
        match mode {
            crate::config::Mode::Arc => self.arc.to_vec(),
            crate::config::Mode::Single => vec![self.led],
        }
    }
}

/// Minimum hue difference (degrees) between two arc colours.
pub const ARC_MIN_HUE_GAP: f32 = 30.0;

#[derive(Debug)]
pub enum ExtractError {
    Image(image::ImageError),
    Empty,
}

impl std::fmt::Display for ExtractError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ExtractError::Image(e) => write!(f, "cannot read image: {e}"),
            ExtractError::Empty => write!(f, "image has no pixels"),
        }
    }
}

impl std::error::Error for ExtractError {}

impl From<image::ImageError> for ExtractError {
    fn from(e: image::ImageError) -> Self {
        ExtractError::Image(e)
    }
}

/// Reads the picture by its content, not its extension (wallpapers are often PNGs named `.jpg`).
pub fn extract_path(
    path: impl AsRef<Path>,
    cfg: &ExtractConfig,
) -> Result<Extraction, ExtractError> {
    let img = image::ImageReader::open(path)
        .map_err(image::ImageError::IoError)?
        .with_guessed_format()
        .map_err(image::ImageError::IoError)?
        .decode()?;
    extract(&img, cfg)
}

pub fn extract(img: &DynamicImage, cfg: &ExtractConfig) -> Result<Extraction, ExtractError> {
    let side = cfg.sample_size.max(1);
    let small = img.resize_exact(side, side, FilterType::Triangle).to_rgb8();
    let pixels: Vec<[u8; 3]> = small.pixels().map(|p| p.0).collect();
    if pixels.is_empty() {
        return Err(ExtractError::Empty);
    }

    let total = pixels.len() as f32;
    let palette: Vec<(Rgb, f32)> = range_cut(pixels, cfg.palette_size.max(1))
        .into_iter()
        .map(|(c, n)| (c, n as f32 / total))
        .collect();

    let mut candidates: Vec<(Rgb, Hsv, f32)> = palette
        .iter()
        .map(|&(c, share)| (c, c.to_hsv(), share))
        .filter(|&(_, hsv, share)| {
            share >= cfg.min_share as f32 && hsv.s * hsv.v >= cfg.min_chroma as f32
        })
        .filter(|&(_, hsv, _)| !is_skin(hsv) || hsv.s * hsv.v >= PALE_SKIN_CHROMA)
        .collect();
    candidates.sort_by(|a, b| vividness(b.1, b.2, cfg).total_cmp(&vividness(a.1, a.2, cfg)));

    let led_of = |hsv: Hsv| {
        Rgb::from_hsv(Hsv {
            h: hsv.h,
            s: led_saturation(hsv, cfg),
            v: 1.0,
        })
    };

    let Some(&(picked, main, _)) = candidates.first() else {
        return Ok(Extraction {
            palette,
            picked: None,
            led: Rgb::WHITE,
            arc: [Rgb::WHITE; 3],
        });
    };

    // Accents: rank the rest by colourfulness (chroma², skin discounted) over √share, so a
    // small purple sparkle beats a large dull slate background.
    let accent_score = |h: Hsv, share: f32| {
        let skin = if is_skin(h) {
            cfg.skin_weight as f32
        } else {
            1.0
        };
        (h.s * h.v).powi(2) * share.sqrt() * skin
    };
    let mut accents: Vec<(Hsv, f32)> = candidates[1..]
        .iter()
        .map(|&(_, h, share)| (h, accent_score(h, share)))
        .collect();
    accents.sort_by(|a, b| b.1.total_cmp(&a.1));
    let mut hues: Vec<Hsv> = vec![main];
    for &(hsv, _) in &accents {
        if hues.len() == 3 {
            break;
        }
        if hues
            .iter()
            .all(|h| hue_distance(h.h, hsv.h) >= ARC_MIN_HUE_GAP)
        {
            hues.push(hsv);
        }
    }
    let led = led_of(main);
    let mut arc: Vec<Rgb> = hues.into_iter().map(led_of).collect();
    if arc.len() < 3 {
        // a lighter shade of the main colour (paler than a pastel main colour too)
        arc.push(Rgb::from_hsv(Hsv {
            h: main.h,
            s: (led.to_hsv().s * 0.5).min(0.35),
            v: 1.0,
        }));
    }
    if arc.len() < 3 {
        // and a dimmer one: deep for vivid colours, only slightly dimmer for pastels
        let led_hsv = led.to_hsv();
        let pastel = led_hsv.s < cfg.min_saturation as f32 * 0.9;
        let v = if pastel { 0.7 } else { 0.45 };
        arc.push(Rgb::from_hsv(Hsv { v, ..led_hsv }));
    }

    Ok(Extraction {
        palette,
        picked: Some(picked),
        led,
        arc: [arc[0], arc[1], arc[2]],
    })
}

fn hue_distance(a: f32, b: f32) -> f32 {
    let d = (a - b).rem_euclid(360.0);
    d.min(360.0 - d)
}

/// Saturation for the LEDs: dark colours raised to `min_saturation`, light ones kept pastel.
fn led_saturation(hsv: Hsv, cfg: &ExtractConfig) -> f32 {
    let floor = cfg.min_saturation as f32;
    let dark = hsv.s.max(floor);
    let light = (hsv.s * cfg.pastel_gain as f32).clamp(hsv.s, floor.max(hsv.s));
    let t = ((hsv.v - 0.55) / (0.8 - 0.55)).clamp(0.0, 1.0);
    let t = t * t * (3.0 - 2.0 * t); // smoothstep
    dark + (light - dark) * t
}

fn vividness(hsv: Hsv, share: f32, cfg: &ExtractConfig) -> f32 {
    let score = share * hsv.s * hsv.s * (0.3 + hsv.v);
    if is_skin(hsv) {
        score * cfg.skin_weight as f32
    } else {
        score
    }
}

/// Skin with less chroma than this (beige, greyish peach) is treated as grey.
const PALE_SKIN_CHROMA: f32 = 0.15;

/// Soft orange, light or in shadow: where anime and photo skin falls (vivid orange is not skin).
fn is_skin(hsv: Hsv) -> bool {
    (5.0..=45.0).contains(&hsv.h) && (0.12..=0.6).contains(&hsv.s) && hsv.v >= 0.2
}

/// Range cut: split the box with the widest channel range at the middle of that range until
/// there are `n` boxes; each box becomes its average colour. Returns (colour, pixel count),
/// largest first.
fn range_cut(pixels: Vec<[u8; 3]>, n: usize) -> Vec<(Rgb, usize)> {
    let mut boxes = vec![pixels];
    while boxes.len() < n {
        let Some((i, channel, mid)) = boxes
            .iter()
            .enumerate()
            .map(|(i, b)| (i, widest_channel(b)))
            .filter(|(_, (_, lo, hi))| hi > lo)
            .max_by_key(|(_, (_, lo, hi))| hi - lo)
            .map(|(i, (ch, lo, hi))| (i, ch, lo + (hi - lo) / 2))
        else {
            break; // every box is a single colour
        };
        let b = boxes.swap_remove(i);
        let (low, high): (Vec<_>, Vec<_>) = b.into_iter().partition(|p| p[channel] <= mid);
        boxes.push(low);
        boxes.push(high);
    }

    let mut out: Vec<(Rgb, usize)> = boxes
        .into_iter()
        .filter(|b| !b.is_empty())
        .map(|b| {
            let n = b.len();
            let sum = b.iter().fold([0u64; 3], |acc, p| {
                [
                    acc[0] + p[0] as u64,
                    acc[1] + p[1] as u64,
                    acc[2] + p[2] as u64,
                ]
            });
            let avg = |i: usize| ((sum[i] + n as u64 / 2) / n as u64) as u8;
            (Rgb::new(avg(0), avg(1), avg(2)), n)
        })
        .collect();
    out.sort_by_key(|&(_, n)| std::cmp::Reverse(n));
    out
}

/// (channel, min, max) of the channel with the widest range.
fn widest_channel(pixels: &[[u8; 3]]) -> (usize, u8, u8) {
    let mut lo = [255u8; 3];
    let mut hi = [0u8; 3];
    for p in pixels {
        for c in 0..3 {
            lo[c] = lo[c].min(p[c]);
            hi[c] = hi[c].max(p[c]);
        }
    }
    (0..3)
        .map(|c| (c, lo[c], hi[c]))
        .max_by_key(|&(_, l, h)| h.saturating_sub(l))
        .unwrap()
}

#[cfg(test)]
mod tests {
    use super::*;
    use image::{Rgb as Px, RgbImage};

    fn image_from(f: impl Fn(u32, u32) -> [u8; 3]) -> DynamicImage {
        DynamicImage::ImageRgb8(RgbImage::from_fn(200, 100, |x, y| Px(f(x, y))))
    }

    #[test]
    fn solid_colour_is_kept_and_brightened() {
        let e = extract(&image_from(|_, _| [200, 20, 20]), &ExtractConfig::default()).unwrap();
        assert_eq!(e.picked, Some(Rgb::new(200, 20, 20)));
        let hsv = e.led.to_hsv();
        assert!(hsv.h < 2.0 || hsv.h > 358.0, "still red: {}", e.led);
        assert_eq!(hsv.v, 1.0);
    }

    #[test]
    fn pastel_is_saturated() {
        let cfg = ExtractConfig::default();
        let e = extract(&image_from(|_, _| [180, 200, 255]), &cfg).unwrap();
        assert!(
            e.led.to_hsv().s >= cfg.min_saturation as f32 - 0.01,
            "{}",
            e.led
        );
    }

    #[test]
    fn grey_picture_gives_white() {
        let e = extract(
            &image_from(|x, _| [(x % 256) as u8; 3]),
            &ExtractConfig::default(),
        )
        .unwrap();
        assert_eq!(e.led, Rgb::WHITE);
    }

    #[test]
    fn specks_are_ignored() {
        // grey picture with a 0.16 % orange speck: white, not orange
        let img = image_from(|x, y| {
            if x < 4 && y < 8 {
                [200, 120, 60]
            } else {
                [90, 90, 92]
            }
        });
        assert_eq!(
            extract(&img, &ExtractConfig::default()).unwrap().led,
            Rgb::WHITE
        );
    }

    #[test]
    fn pale_skin_on_grey_gives_white() {
        let img = image_from(|x, _| {
            if x < 30 {
                [153, 139, 127]
            } else {
                [33, 40, 39]
            }
        });
        assert_eq!(
            extract(&img, &ExtractConfig::default()).unwrap().led,
            Rgb::WHITE
        );
    }

    #[test]
    fn navy_background_beats_pink_hair() {
        let img = image_from(|x, _| match x {
            0..60 => [64, 64, 82],
            60..75 => [138, 98, 101],
            _ => [21, 21, 30],
        });
        let hsv = extract(&img, &ExtractConfig::default())
            .unwrap()
            .led
            .to_hsv();
        assert!(
            (220.0..260.0).contains(&hsv.h),
            "navy/blue expected, got hue {}",
            hsv.h
        );
    }

    #[test]
    fn skin_loses_to_a_smaller_real_colour() {
        // 60 % skin, 15 % blue hair, rest dark: blue
        let img = image_from(|x, _| match x {
            0..120 => [230, 190, 160],
            120..150 => [60, 90, 200],
            _ => [30, 30, 35],
        });
        let e = extract(&img, &ExtractConfig::default()).unwrap();
        assert!(
            (215.0..240.0).contains(&e.led.to_hsv().h),
            "blue expected, got {}",
            e.led
        );
    }

    #[test]
    fn skin_only_picture_still_gets_a_colour() {
        let e = extract(
            &image_from(|_, _| [230, 190, 160]),
            &ExtractConfig::default(),
        )
        .unwrap();
        assert_ne!(e.led, Rgb::WHITE);
    }

    #[test]
    fn light_pastel_stays_pastel() {
        // pale peach (12 % saturation, bright): soft peach, not orange
        let e = extract(
            &image_from(|_, _| [226, 207, 200]),
            &ExtractConfig::default(),
        )
        .unwrap();
        let hsv = e.led.to_hsv();
        assert!(
            (0.25..0.4).contains(&hsv.s),
            "pastel kept: {} (s {})",
            e.led,
            hsv.s
        );
        assert!(
            e.arc.iter().all(|c| c.to_hsv().v >= 0.69),
            "no deep shade: {:?}",
            e.arc
        );
    }

    #[test]
    fn dark_muted_colour_is_still_boosted() {
        // shaded rose (pink hair in shadow): raised to the saturation floor
        let e = extract(&image_from(|_, _| [95, 53, 54]), &ExtractConfig::default()).unwrap();
        let min = ExtractConfig::default().min_saturation as f32;
        assert!(e.led.to_hsv().s >= min - 0.01, "{}", e.led);
    }

    #[test]
    fn small_purple_accent_beats_large_dull_background() {
        let img = image_from(|x, _| match x {
            0..60 => [61, 63, 79],      // dull slate, 30 %
            60..100 => [88, 124, 100],  // green
            100..120 => [175, 96, 100], // red (main)
            120..122 => [206, 86, 218], // purple sparkle, 1 %
            _ => [34, 37, 39],
        });
        let e = extract(&img, &ExtractConfig::default()).unwrap();
        assert!(
            e.arc.iter().any(|c| (270.0..310.0).contains(&c.to_hsv().h)),
            "purple kept: {:?}",
            e.arc
        );
    }

    #[test]
    fn tinted_near_black_is_grey() {
        // Mostly an almost-black with a faint purple tint (high saturation, tiny chroma): white
        let img = image_from(|x, _| if x < 100 { [27, 25, 30] } else { [70, 72, 75] });
        let e = extract(&img, &ExtractConfig::default()).unwrap();
        assert_eq!(e.picked, None);
        assert_eq!(e.led, Rgb::WHITE);
    }

    #[test]
    fn dark_but_vivid_colour_counts() {
        let e = extract(&image_from(|_, _| [3, 19, 105]), &ExtractConfig::default()).unwrap();
        assert!(
            (225.0..240.0).contains(&e.led.to_hsv().h),
            "deep blue kept: {}",
            e.led
        );
    }

    #[test]
    fn vivid_minority_beats_grey_majority() {
        // 80 % grey, 20 % blue: the LEDs should be blue, not white
        let img = image_from(|x, _| {
            if x < 160 {
                [120, 120, 125]
            } else {
                [20, 60, 230]
            }
        });
        let e = extract(&img, &ExtractConfig::default()).unwrap();
        let hsv = e.led.to_hsv();
        assert!(
            (200.0..250.0).contains(&hsv.h),
            "blue expected, got {}",
            e.led
        );
    }

    #[test]
    fn palette_shares_sum_to_one() {
        let img = image_from(|x, y| [(x % 256) as u8, (y * 2 % 256) as u8, 90]);
        let e = extract(&img, &ExtractConfig::default()).unwrap();
        let sum: f32 = e.palette.iter().map(|p| p.1).sum();
        assert!((sum - 1.0).abs() < 1e-4, "{sum}");
        assert!(e.palette.len() <= 10);
        assert!(
            e.palette.windows(2).all(|w| w[0].1 >= w[1].1),
            "sorted by share"
        );
    }

    #[test]
    fn format_comes_from_content_not_extension() {
        let dir = std::env::temp_dir().join(format!("wpc-test-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("really-a-png.jpg");
        RgbImage::from_pixel(4, 4, Px([0, 0, 255]))
            .save_with_format(&path, image::ImageFormat::Png)
            .unwrap();
        let e = extract_path(&path, &ExtractConfig::default()).unwrap();
        std::fs::remove_dir_all(&dir).unwrap();
        assert_eq!(e.led, Rgb::new(0, 0, 255));
    }

    #[test]
    fn arc_takes_three_distinct_hues() {
        let img = image_from(|x, _| match x {
            0..50 => [220, 30, 30],    // red
            50..60 => [230, 60, 40],   // another red: too close, skipped
            60..110 => [30, 60, 220],  // blue
            110..150 => [40, 200, 60], // green
            _ => [20, 20, 20],
        });
        let e = extract(&img, &ExtractConfig::default()).unwrap();
        assert_eq!(e.arc[0], e.led);
        let hues: Vec<f32> = e.arc.iter().map(|c| c.to_hsv().h).collect();
        for i in 0..3 {
            for j in i + 1..3 {
                assert!(
                    hue_distance(hues[i], hues[j]) >= ARC_MIN_HUE_GAP,
                    "{:?}",
                    e.arc
                );
            }
        }
    }

    #[test]
    fn single_colour_picture_gets_shades_in_the_arc() {
        let e = extract(&image_from(|_, _| [30, 60, 220]), &ExtractConfig::default()).unwrap();
        let [a, b, c] = e.arc.map(|c| c.to_hsv());
        assert!(hue_distance(a.h, b.h) < 5.0 && hue_distance(a.h, c.h) < 5.0);
        assert!(b.s < a.s, "lighter shade");
        assert!(c.v < a.v, "dimmer shade");
    }

    #[test]
    fn colourless_picture_arc_is_white() {
        let e = extract(
            &image_from(|x, _| [(x % 256) as u8; 3]),
            &ExtractConfig::default(),
        )
        .unwrap();
        assert_eq!(e.arc, [Rgb::WHITE; 3]);
    }

    #[test]
    fn single_pixel_image() {
        let img = DynamicImage::ImageRgb8(RgbImage::from_pixel(1, 1, Px([0, 255, 0])));
        assert_eq!(
            extract(&img, &ExtractConfig::default()).unwrap().led,
            Rgb::new(0, 255, 0)
        );
    }
}
