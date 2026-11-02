//! Protocol between the service and its clients (the terminal UI and commands).
//!
//! A Unix socket at `$XDG_RUNTIME_DIR/wallpapercolor.sock`; one request per connection: the
//! client writes a JSON [`Request`] line, the service answers with a JSON [`Response`] line.
//! Settings are not part of it: clients edit the config file and the service reloads it.

use crate::config::Mode;
use serde::{Deserialize, Serialize};
use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixStream;
use std::path::PathBuf;
use std::time::Duration;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "cmd", rename_all = "snake_case")]
pub enum Request {
    Status,
    /// Stop sending colours until `Resume` (in memory only; a restart resumes).
    Pause,
    Resume,
    /// Send the current wallpaper's colours again now.
    Apply,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "result", rename_all = "snake_case")]
pub enum Response {
    Status(Status),
    Ok,
    Error { message: String },
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
pub struct Status {
    pub version: String,
    pub paused: bool,
    pub mode: Mode,
    /// Wallpaper plugin (`org.kde.image`, `org.kde.slideshow`…).
    pub plugin: String,
    /// Picture the colours come from.
    pub image: Option<PathBuf>,
    /// Gradient stops last computed, as `#rrggbb`.
    pub stops: Vec<String>,
    /// Devices of the last successful send.
    pub devices: Vec<DeviceInfo>,
    /// Seconds since the Unix epoch of the last successful send.
    pub last_sent: Option<u64>,
    /// Last problem (OpenLinkHub or Plasma unreachable…), cleared by the next success.
    pub error: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct DeviceInfo {
    pub name: String,
    /// OpenRGB device type (1 = RAM).
    pub kind: i32,
    pub zones: Vec<ZoneInfo>,
}

#[derive(Debug, Clone, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct ZoneInfo {
    pub name: String,
    pub leds: u32,
}

/// `$XDG_RUNTIME_DIR/wallpapercolor.sock` (or `/tmp/wallpapercolor-$USER.sock`).
pub fn socket_path() -> PathBuf {
    match std::env::var_os("XDG_RUNTIME_DIR")
        .map(PathBuf::from)
        .filter(|p| p.is_absolute())
    {
        Some(dir) => dir.join("wallpapercolor.sock"),
        None => {
            let user = std::env::var("USER").unwrap_or_else(|_| "user".into());
            PathBuf::from(format!("/tmp/wallpapercolor-{user}.sock"))
        }
    }
}

/// Sends one request to the running service and waits (up to 3 s) for the answer.
pub fn call(request: &Request) -> std::io::Result<Response> {
    let mut stream = UnixStream::connect(socket_path())?;
    stream.set_read_timeout(Some(Duration::from_secs(3)))?;
    stream.set_write_timeout(Some(Duration::from_secs(3)))?;
    let mut line = serde_json::to_string(request).map_err(std::io::Error::other)?;
    line.push('\n');
    stream.write_all(line.as_bytes())?;
    let mut reply = String::new();
    BufReader::new(stream).read_line(&mut reply)?;
    serde_json::from_str(&reply).map_err(std::io::Error::other)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wire_format_is_stable_json() {
        assert_eq!(
            serde_json::to_string(&Request::Status).unwrap(),
            r#"{"cmd":"status"}"#
        );
        assert_eq!(
            serde_json::to_string(&Response::Ok).unwrap(),
            r#"{"result":"ok"}"#
        );
        let err = Response::Error {
            message: "x".into(),
        };
        assert_eq!(
            serde_json::to_string(&err).unwrap(),
            r#"{"result":"error","message":"x"}"#
        );
    }

    #[test]
    fn status_round_trip() {
        let s = Status {
            version: "0.1.0".into(),
            mode: Mode::Arc,
            stops: vec!["#ff0000".into()],
            devices: vec![DeviceInfo {
                name: "RAM".into(),
                kind: 1,
                zones: vec![ZoneInfo {
                    name: "DIMM".into(),
                    leds: 10,
                }],
            }],
            ..Default::default()
        };
        let json = serde_json::to_string(&Response::Status(s.clone())).unwrap();
        assert_eq!(
            serde_json::from_str::<Response>(&json).unwrap(),
            Response::Status(s)
        );
    }
}
