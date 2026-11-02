//! Picks one LED-friendly colour from a wallpaper.
//!
//! 1. Downscale the picture (only the overall colours matter, and it keeps this fast).
//! 2. Reduce it to a small palette by repeatedly cutting the colour box with the widest range
//!    at the middle of that range (not at the median pixel: in a mostly-black picture that only
//!    slices the black into identical pieces and averages small vivid areas away).
//! 3. Ignore near-grey entries (chroma below `min_chroma`). Chroma, the gap between the highest
//!    and lowest channel, is used instead of saturation because saturation blows up on very dark
//!    colours: an almost-black `#1b191e` has 17 % saturation but only 2 % chroma.
//! 4. Pick the most *vivid* remaining entry: `share × saturation² × (0.3 + value)`, so a bright
//!    character on a grey background wins over the background.
//! 5. Push it to full brightness and at least `min_saturation`: LEDs make pastel shades look
//!    washed out. A picture with no real colour (black and white) gives white.

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
    pub min_saturation: f32,
    /// Palette entries with less chroma than this (max − min channel, 0–1) count as grey.
    /// When every entry is grey the picture is colourless and the LEDs go white.
    pub min_chroma: f32,
}

impl Default for ExtractConfig {
    fn default() -> Self {
        Self {
            palette_size: 10,
            sample_size: 96,
            min_saturation: 0.8,
            min_chroma: 0.08,
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
}

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

    let best = palette
        .iter()
        .map(|&(c, share)| (c, c.to_hsv(), share))
        .filter(|(_, hsv, _)| hsv.s * hsv.v >= cfg.min_chroma)
        .max_by(|a, b| vividness(a.1, a.2).total_cmp(&vividness(b.1, b.2)));

    let led = match best {
        Some((_, hsv, _)) => Rgb::from_hsv(Hsv {
            h: hsv.h,
            s: hsv.s.max(cfg.min_saturation),
            v: 1.0,
        }),
        None => Rgb::WHITE,
    };
    Ok(Extraction {
        palette,
        picked: best.map(|(c, _, _)| c),
        led,
    })
}

fn vividness(hsv: Hsv, share: f32) -> f32 {
    share * hsv.s * hsv.s * (0.3 + hsv.v)
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
        let e = extract(
            &image_from(|_, _| [180, 200, 255]),
            &ExtractConfig::default(),
        )
        .unwrap();
        assert!(e.led.to_hsv().s >= 0.79, "{}", e.led);
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
    fn single_pixel_image() {
        let img = DynamicImage::ImageRgb8(RgbImage::from_pixel(1, 1, Px([0, 255, 0])));
        assert_eq!(
            extract(&img, &ExtractConfig::default()).unwrap().led,
            Rgb::new(0, 255, 0)
        );
    }
}
