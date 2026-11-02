//! User configuration: `~/.config/wallpapercolor/config.toml`.
//!
//! Every field has a default, so a missing file or a partial file is fine.

use crate::extract::ExtractConfig;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::{fmt, fs, io};

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Config {
    pub led: LedConfig,
    pub extract: ExtractConfig,
    pub openrgb: OpenRgbConfig,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Mode {
    /// Three colours from the picture as a gradient: around each ring, top to bottom on RAM.
    #[default]
    Arc,
    /// The main colour on every LED.
    Single,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct LedConfig {
    pub mode: Mode,
}

/// Where to send colours: an OpenRGB SDK server.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct OpenRgbConfig {
    /// `host:port`. OpenLinkHub's built-in server listens on 6743 (`enableOpenRGBTargetServer`);
    /// OpenRGB itself on 6742.
    pub address: String,
    /// Name shown to the server for this client.
    pub client_name: String,
}

impl Default for OpenRgbConfig {
    fn default() -> Self {
        Self {
            address: "127.0.0.1:6743".into(),
            client_name: "WallpaperColor".into(),
        }
    }
}

#[derive(Debug)]
pub enum ConfigError {
    Io(PathBuf, io::Error),
    Parse(PathBuf, toml::de::Error),
}

impl fmt::Display for ConfigError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ConfigError::Io(p, e) => write!(f, "{}: {e}", p.display()),
            ConfigError::Parse(p, e) => write!(f, "{}: {e}", p.display()),
        }
    }
}

impl std::error::Error for ConfigError {}

impl Config {
    /// `$XDG_CONFIG_HOME/wallpapercolor/config.toml` (or `~/.config/...`).
    pub fn default_path() -> Option<PathBuf> {
        let base = std::env::var_os("XDG_CONFIG_HOME")
            .map(PathBuf::from)
            .filter(|p| p.is_absolute())
            .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))?;
        Some(base.join("wallpapercolor").join("config.toml"))
    }

    /// Reads the file; a missing file gives the defaults.
    pub fn load(path: &Path) -> Result<Self, ConfigError> {
        match fs::read_to_string(path) {
            Ok(text) => Self::from_toml(&text).map_err(|e| ConfigError::Parse(path.to_owned(), e)),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(Self::default()),
            Err(e) => Err(ConfigError::Io(path.to_owned(), e)),
        }
    }

    pub fn from_toml(text: &str) -> Result<Self, toml::de::Error> {
        toml::from_str(text)
    }

    pub fn to_toml(&self) -> String {
        toml::to_string_pretty(self).expect("config always serialises")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_file_gives_defaults() {
        assert_eq!(Config::from_toml("").unwrap(), Config::default());
    }

    #[test]
    fn partial_file_keeps_other_defaults() {
        let c = Config::from_toml("[extract]\nmin_saturation = 0.5\n").unwrap();
        assert_eq!(c.extract.min_saturation, 0.5);
        assert_eq!(
            c.extract.palette_size,
            ExtractConfig::default().palette_size
        );
    }

    #[test]
    fn mode_is_arc_by_default_and_parses() {
        assert_eq!(Config::default().led.mode, Mode::Arc);
        let c = Config::from_toml("[led]\nmode = \"single\"\n").unwrap();
        assert_eq!(c.led.mode, Mode::Single);
        assert!(Config::from_toml("[led]\nmode = \"rainbow\"\n").is_err());
    }

    #[test]
    fn unknown_keys_are_rejected() {
        assert!(Config::from_toml("[extract]\nmin_saturaton = 0.5\n").is_err());
    }

    #[test]
    fn round_trip() {
        let c = Config::default();
        assert_eq!(Config::from_toml(&c.to_toml()).unwrap(), c);
    }

    #[test]
    fn missing_file_gives_defaults() {
        let c = Config::load(Path::new("/nonexistent/wallpapercolor.toml")).unwrap();
        assert_eq!(c, Config::default());
    }
}
