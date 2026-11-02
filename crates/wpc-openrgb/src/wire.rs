//! Decoding of SDK payloads (`REQUEST_CONTROLLER_DATA`).

use crate::{Controller, Error, Result, Rgb, Zone};

pub(crate) struct Reader<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    pub(crate) fn new(data: &'a [u8]) -> Self {
        Self { data, pos: 0 }
    }

    fn take(&mut self, n: usize) -> Result<&'a [u8]> {
        let end = self
            .pos
            .checked_add(n)
            .filter(|&e| e <= self.data.len())
            .ok_or_else(|| {
                Error::Protocol(format!(
                    "truncated payload ({n} bytes needed at offset {})",
                    self.pos
                ))
            })?;
        let s = &self.data[self.pos..end];
        self.pos = end;
        Ok(s)
    }

    pub(crate) fn u16(&mut self) -> Result<u16> {
        Ok(u16::from_le_bytes(self.take(2)?.try_into().unwrap()))
    }

    pub(crate) fn u32(&mut self) -> Result<u32> {
        Ok(u32::from_le_bytes(self.take(4)?.try_into().unwrap()))
    }

    pub(crate) fn i32(&mut self) -> Result<i32> {
        Ok(self.u32()? as i32)
    }

    pub(crate) fn string(&mut self) -> Result<String> {
        let len = self.u16()? as usize;
        let bytes = self.take(len)?;
        let bytes = bytes.strip_suffix(&[0]).unwrap_or(bytes);
        Ok(String::from_utf8_lossy(bytes).into_owned())
    }

    fn skip(&mut self, n: usize) -> Result<()> {
        self.take(n).map(|_| ())
    }
}

/// Parses a controller description as sent for protocol `proto` (0–4).
pub(crate) fn parse_controller(data: &[u8], index: u32, proto: u32) -> Result<Controller> {
    let mut r = Reader::new(data);
    let _size = r.u32()?;
    let kind = r.i32()?;
    let name = r.string()?;
    let vendor = if proto >= 1 {
        r.string()?
    } else {
        String::new()
    };
    let description = r.string()?;
    let version = r.string()?;
    let serial = r.string()?;
    let location = r.string()?;

    let modes = r.u16()?;
    let _active_mode = r.i32()?;
    for _ in 0..modes {
        r.string()?; // name
        r.skip(4 + 4 + 4 + 4)?; // value, flags, speed_min, speed_max
        if proto >= 3 {
            r.skip(4 + 4)?; // brightness_min, brightness_max
        }
        r.skip(4 + 4 + 4)?; // colors_min, colors_max, speed
        if proto >= 3 {
            r.skip(4)?; // brightness
        }
        r.skip(4 + 4)?; // direction, color_mode
        let colors = r.u16()? as usize;
        r.skip(4 * colors)?;
    }

    let zone_count = r.u16()?;
    let mut zones = Vec::with_capacity(zone_count as usize);
    for _ in 0..zone_count {
        let name = r.string()?;
        let kind = r.i32()?;
        r.skip(4 + 4)?; // leds_min, leds_max
        let leds = r.u32()?;
        let matrix_len = r.u16()? as usize; // bytes: height + width + entries
        r.skip(matrix_len)?;
        if proto >= 4 {
            let segments = r.u16()?;
            for _ in 0..segments {
                r.string()?;
                r.skip(4 + 4 + 4)?; // type, start, count
            }
        }
        zones.push(Zone { name, kind, leds });
    }

    let leds = r.u16()?;
    for _ in 0..leds {
        r.string()?;
        r.skip(4)?; // value
    }

    let color_count = r.u16()? as usize;
    let mut colors = Vec::with_capacity(color_count);
    for _ in 0..color_count {
        let c = r.take(4)?;
        colors.push(Rgb::new(c[0], c[1], c[2]));
    }

    Ok(Controller {
        index,
        kind,
        name,
        vendor,
        description,
        version,
        serial,
        location,
        zones,
        colors,
    })
}
