//! Gradients that spread a few colours over a row or ring of LEDs.
//!
//! Interpolation goes through HSV along the shorter way round the hue circle, so blue to orange
//! passes through vivid colours instead of the grey an RGB blend would give.

use crate::color::{Hsv, Rgb};

/// `n` colours going through `stops` in order. `closed` (a ring) wraps back to the first stop so
/// there is no jump where the ring closes; otherwise the first and last LED get the end stops.
pub fn gradient(stops: &[Rgb], n: usize, closed: bool) -> Vec<Rgb> {
    match (stops.len(), n) {
        (_, 0) => return Vec::new(),
        (0, _) => return vec![Rgb::default(); n],
        (1, _) => return vec![stops[0]; n],
        _ => {}
    }
    let segments = if closed { stops.len() } else { stops.len() - 1 };
    (0..n)
        .map(|i| {
            let t = if closed {
                i as f32 / n as f32
            } else if n == 1 {
                0.0
            } else {
                i as f32 / (n - 1) as f32
            } * segments as f32;
            let seg = (t.floor() as usize).min(segments - 1);
            let a = stops[seg];
            let b = stops[(seg + 1) % stops.len()];
            mix(a, b, t - seg as f32)
        })
        .collect()
}

/// HSV blend of `a` and `b` (`t` = 0 gives `a`). A colourless end takes the other end's hue.
pub fn mix(a: Rgb, b: Rgb, t: f32) -> Rgb {
    let (ha, hb) = (a.to_hsv(), b.to_hsv());
    let ha_h = if ha.s == 0.0 { hb.h } else { ha.h };
    let hb_h = if hb.s == 0.0 { ha_h } else { hb.h };
    let mut dh = hb_h - ha_h;
    if dh > 180.0 {
        dh -= 360.0;
    } else if dh < -180.0 {
        dh += 360.0;
    }
    Rgb::from_hsv(Hsv {
        h: (ha_h + dh * t).rem_euclid(360.0),
        s: ha.s + (hb.s - ha.s) * t,
        v: ha.v + (hb.v - ha.v) * t,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    const R: Rgb = Rgb::new(255, 0, 0);
    const G: Rgb = Rgb::new(0, 255, 0);
    const B: Rgb = Rgb::new(0, 0, 255);

    #[test]
    fn open_gradient_hits_both_ends() {
        let g = gradient(&[R, G, B], 10, false);
        assert_eq!(g.len(), 10);
        assert_eq!(g[0], R);
        assert_eq!(g[9], B);
    }

    #[test]
    fn closed_gradient_wraps_without_a_jump() {
        let g = gradient(&[R, G, B], 9, true);
        assert_eq!(g[0], R);
        assert_eq!(g[3], G);
        assert_eq!(g[6], B);
        // the last LED is between blue and red again (magenta side), not blue
        let h = g[8].to_hsv().h;
        assert!((280.0..340.0).contains(&h), "hue {h}");
    }

    #[test]
    fn mix_takes_the_short_way_round_and_stays_vivid() {
        let m = mix(Rgb::new(255, 0, 40), Rgb::new(255, 40, 0), 0.5); // across hue 0
        let h = m.to_hsv();
        assert!(h.h < 10.0 || h.h > 350.0, "hue {}", h.h);
        let blue_orange = mix(B, Rgb::new(255, 128, 0), 0.5).to_hsv();
        assert!(blue_orange.s > 0.9, "no grey midpoint");
    }

    #[test]
    fn white_end_keeps_the_other_hue() {
        let m = mix(Rgb::WHITE, B, 0.5).to_hsv();
        assert!((235.0..245.0).contains(&m.h));
    }

    #[test]
    fn edge_cases() {
        assert!(gradient(&[R, G], 0, true).is_empty());
        assert_eq!(gradient(&[R], 3, false), vec![R; 3]);
        assert_eq!(gradient(&[R, G, B], 1, false), vec![R]);
    }
}
