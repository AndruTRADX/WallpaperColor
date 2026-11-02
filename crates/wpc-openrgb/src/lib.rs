//! Minimal synchronous OpenRGB SDK client.
//!
//! Speaks the subset of the OpenRGB SDK protocol (up to version 4) needed to list controllers
//! and set their LEDs. Used against OpenLinkHub's built-in SDK server (port 6743), which only
//! exposes devices whose "OpenRGB Integration" toggle is on, and works with OpenRGB itself.
//!
//! Wire format: every packet is a 16-byte header (`"ORGB"`, device index, packet id, payload
//! size; little endian) followed by the payload. Strings are `u16 length` (NUL included) + bytes.

mod wire;

use std::io::{self, Read, Write};
use std::net::{TcpStream, ToSocketAddrs};
use std::time::Duration;
use wire::Reader;
pub use wpc_core::Rgb;
use wpc_core::arc::gradient;

/// Highest protocol version this client understands.
pub const PROTOCOL_VERSION: u32 = 4;

mod packet {
    pub const REQUEST_CONTROLLER_COUNT: u32 = 0;
    pub const REQUEST_CONTROLLER_DATA: u32 = 1;
    pub const REQUEST_PROTOCOL_VERSION: u32 = 40;
    pub const SET_CLIENT_NAME: u32 = 50;
    pub const DEVICE_LIST_UPDATED: u32 = 100;
    pub const UPDATE_LEDS: u32 = 1050;
}

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    /// The server sent something this client does not understand.
    Protocol(String),
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Error::Io(e) => write!(f, "{e}"),
            Error::Protocol(m) => write!(f, "OpenRGB protocol error: {m}"),
        }
    }
}

impl std::error::Error for Error {}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Error::Io(e)
    }
}

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Zone {
    pub name: String,
    pub kind: i32,
    pub leds: u32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Controller {
    /// Index used to address the controller in this session (it can change when devices change).
    pub index: u32,
    pub kind: i32,
    pub name: String,
    pub vendor: String,
    pub description: String,
    pub version: String,
    pub serial: String,
    pub location: String,
    pub zones: Vec<Zone>,
    /// Current colour of every LED, in LED order.
    pub colors: Vec<Rgb>,
}

fn scale(c: Rgb, [r, g, b]: [f32; 3]) -> Rgb {
    let s = |v: u8, k: f32| (v as f32 * k).round().clamp(0.0, 255.0) as u8;
    Rgb::new(s(c.r, r), s(c.g, g), s(c.b, b))
}

/// OpenRGB device type of RAM modules.
pub const DEVICE_TYPE_DRAM: i32 = 1;

impl Controller {
    pub fn led_count(&self) -> usize {
        self.colors.len()
    }

    /// Colours for every LED spreading `stops` as one gradient per zone: top to bottom on RAM,
    /// a seamless ring elsewhere (fans, pumps and strips tend to be loops). One stop paints
    /// everything that colour.
    pub fn paint(&self, stops: &[Rgb]) -> Vec<Rgb> {
        self.paint_calibrated(stops, |_| [1.0; 3])
    }

    /// Like [`paint`](Self::paint), then scales each zone's red, green and blue by
    /// `gain(zone name)` to correct LEDs that render colours off.
    pub fn paint_calibrated(&self, stops: &[Rgb], gain: impl Fn(&str) -> [f32; 3]) -> Vec<Rgb> {
        let ring = self.kind != DEVICE_TYPE_DRAM;
        let zoned: u32 = self.zones.iter().map(|z| z.leds).sum();
        if zoned as usize != self.led_count() {
            let g = gain("");
            return gradient(stops, self.led_count(), ring)
                .into_iter()
                .map(|c| scale(c, g))
                .collect();
        }
        self.zones
            .iter()
            .flat_map(|z| {
                let g = gain(&z.name);
                gradient(stops, z.leds as usize, ring)
                    .into_iter()
                    .map(move |c| scale(c, g))
            })
            .collect()
    }
}

pub struct Client {
    stream: TcpStream,
    protocol: u32,
}

impl Client {
    /// Connects, announces `client_name` and negotiates the protocol version.
    pub fn connect(addr: impl ToSocketAddrs, client_name: &str) -> Result<Self> {
        let addr = addr
            .to_socket_addrs()?
            .next()
            .ok_or_else(|| Error::Protocol("no address to connect to".into()))?;
        let stream = TcpStream::connect_timeout(&addr, Duration::from_secs(2))?;
        stream.set_read_timeout(Some(Duration::from_secs(5)))?;
        stream.set_write_timeout(Some(Duration::from_secs(5)))?;
        stream.set_nodelay(true)?;
        let mut client = Client {
            stream,
            protocol: 0,
        };

        let mut name = client_name.as_bytes().to_vec();
        name.push(0);
        client.send(0, packet::SET_CLIENT_NAME, &name)?;

        client.send(
            0,
            packet::REQUEST_PROTOCOL_VERSION,
            &PROTOCOL_VERSION.to_le_bytes(),
        )?;
        let reply = client.recv(packet::REQUEST_PROTOCOL_VERSION)?;
        let server = Reader::new(&reply).u32()?;
        client.protocol = server.min(PROTOCOL_VERSION);
        Ok(client)
    }

    /// Protocol version agreed with the server.
    pub fn protocol(&self) -> u32 {
        self.protocol
    }

    pub fn controller_count(&mut self) -> Result<u32> {
        self.send(0, packet::REQUEST_CONTROLLER_COUNT, &[])?;
        let reply = self.recv(packet::REQUEST_CONTROLLER_COUNT)?;
        Reader::new(&reply).u32()
    }

    pub fn controller(&mut self, index: u32) -> Result<Controller> {
        self.send(
            index,
            packet::REQUEST_CONTROLLER_DATA,
            &self.protocol.to_le_bytes(),
        )?;
        let reply = self.recv(packet::REQUEST_CONTROLLER_DATA)?;
        if reply.is_empty() {
            return Err(Error::Protocol(format!("no controller with index {index}")));
        }
        wire::parse_controller(&reply, index, self.protocol)
    }

    pub fn controllers(&mut self) -> Result<Vec<Controller>> {
        (0..self.controller_count()?)
            .map(|i| self.controller(i))
            .collect()
    }

    /// Sets every LED of controller `index`, in LED order. `colors` must not be longer than
    /// the controller's LED count.
    pub fn set_leds(&mut self, index: u32, colors: &[Rgb]) -> Result<()> {
        let count = u16::try_from(colors.len())
            .map_err(|_| Error::Protocol(format!("too many LEDs: {}", colors.len())))?;
        let size = 4 + 2 + 4 * colors.len();
        let mut payload = Vec::with_capacity(size);
        payload.extend_from_slice(&(size as u32).to_le_bytes());
        payload.extend_from_slice(&count.to_le_bytes());
        for c in colors {
            payload.extend_from_slice(&[c.r, c.g, c.b, 0]);
        }
        self.send(index, packet::UPDATE_LEDS, &payload)
    }

    /// Paints every LED of `controller` with one colour.
    pub fn set_all(&mut self, controller: &Controller, color: Rgb) -> Result<()> {
        self.set_leds(controller.index, &vec![color; controller.led_count()])
    }

    fn send(&mut self, device: u32, id: u32, payload: &[u8]) -> Result<()> {
        let mut buf = Vec::with_capacity(16 + payload.len());
        buf.extend_from_slice(b"ORGB");
        buf.extend_from_slice(&device.to_le_bytes());
        buf.extend_from_slice(&id.to_le_bytes());
        buf.extend_from_slice(&(payload.len() as u32).to_le_bytes());
        buf.extend_from_slice(payload);
        self.stream.write_all(&buf)?;
        Ok(())
    }

    /// Reads packets until one with id `want` arrives; device-list notifications are skipped.
    fn recv(&mut self, want: u32) -> Result<Vec<u8>> {
        loop {
            let mut header = [0u8; 16];
            self.stream.read_exact(&mut header)?;
            if &header[0..4] != b"ORGB" {
                return Err(Error::Protocol("bad packet magic".into()));
            }
            let id = u32::from_le_bytes(header[8..12].try_into().unwrap());
            let size = u32::from_le_bytes(header[12..16].try_into().unwrap()) as usize;
            if size > 16 << 20 {
                return Err(Error::Protocol(format!("packet too large: {size} bytes")));
            }
            let mut payload = vec![0u8; size];
            self.stream.read_exact(&mut payload)?;
            if id == want {
                return Ok(payload);
            }
            if id != packet::DEVICE_LIST_UPDATED {
                return Err(Error::Protocol(format!("expected packet {want}, got {id}")));
            }
        }
    }
}

#[cfg(test)]
mod tests;
