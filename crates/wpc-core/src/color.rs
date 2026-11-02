//! 8-bit RGB colour with HSV conversions and hex formatting.

use std::fmt;
use std::str::FromStr;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct Rgb {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

/// Hue in degrees [0, 360), saturation and value in [0, 1].
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Hsv {
    pub h: f32,
    pub s: f32,
    pub v: f32,
}

impl Rgb {
    pub const WHITE: Rgb = Rgb::new(255, 255, 255);

    pub const fn new(r: u8, g: u8, b: u8) -> Self {
        Self { r, g, b }
    }

    pub fn to_hsv(self) -> Hsv {
        let (r, g, b) = (
            self.r as f32 / 255.0,
            self.g as f32 / 255.0,
            self.b as f32 / 255.0,
        );
        let max = r.max(g).max(b);
        let min = r.min(g).min(b);
        let delta = max - min;
        let h = if delta == 0.0 {
            0.0
        } else if max == r {
            60.0 * ((g - b) / delta).rem_euclid(6.0)
        } else if max == g {
            60.0 * ((b - r) / delta + 2.0)
        } else {
            60.0 * ((r - g) / delta + 4.0)
        };
        let s = if max == 0.0 { 0.0 } else { delta / max };
        Hsv { h, s, v: max }
    }

    pub fn from_hsv(hsv: Hsv) -> Self {
        let Hsv { h, s, v } = hsv;
        let c = v * s;
        let hp = h.rem_euclid(360.0) / 60.0;
        let x = c * (1.0 - (hp % 2.0 - 1.0).abs());
        let (r, g, b) = match hp as u32 {
            0 => (c, x, 0.0),
            1 => (x, c, 0.0),
            2 => (0.0, c, x),
            3 => (0.0, x, c),
            4 => (x, 0.0, c),
            _ => (c, 0.0, x),
        };
        let m = v - c;
        let to8 = |f: f32| ((f + m) * 255.0).round().clamp(0.0, 255.0) as u8;
        Rgb::new(to8(r), to8(g), to8(b))
    }
}

impl fmt::Display for Rgb {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "#{:02x}{:02x}{:02x}", self.r, self.g, self.b)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParseRgbError(pub String);

impl fmt::Display for ParseRgbError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "invalid colour {:?}: expected #rrggbb or rrggbb", self.0)
    }
}

impl std::error::Error for ParseRgbError {}

impl FromStr for Rgb {
    type Err = ParseRgbError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let hex = s.strip_prefix('#').unwrap_or(s);
        let err = || ParseRgbError(s.to_owned());
        if hex.len() != 6 || !hex.is_ascii() {
            return Err(err());
        }
        let byte = |i: usize| u8::from_str_radix(&hex[i..i + 2], 16).map_err(|_| err());
        Ok(Rgb::new(byte(0)?, byte(2)?, byte(4)?))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hex_round_trip() {
        let c: Rgb = "#0a77ff".parse().unwrap();
        assert_eq!(c, Rgb::new(0x0a, 0x77, 0xff));
        assert_eq!(c.to_string(), "#0a77ff");
        assert_eq!("ff0000".parse::<Rgb>().unwrap(), Rgb::new(255, 0, 0));
        assert!("#12345".parse::<Rgb>().is_err());
        assert!("#gg0000".parse::<Rgb>().is_err());
    }

    #[test]
    fn hsv_round_trip() {
        for c in [
            Rgb::new(255, 0, 0),
            Rgb::new(0, 255, 0),
            Rgb::new(0, 0, 255),
            Rgb::new(10, 119, 255),
            Rgb::new(200, 30, 180),
            Rgb::new(128, 128, 128),
            Rgb::new(0, 0, 0),
        ] {
            assert_eq!(Rgb::from_hsv(c.to_hsv()), c, "{c}");
        }
    }

    #[test]
    fn hsv_values() {
        let hsv = Rgb::new(255, 0, 0).to_hsv();
        assert_eq!((hsv.h, hsv.s, hsv.v), (0.0, 1.0, 1.0));
        let grey = Rgb::new(128, 128, 128).to_hsv();
        assert_eq!(grey.s, 0.0);
    }
}
