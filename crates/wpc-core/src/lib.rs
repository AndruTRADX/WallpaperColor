//! WallpaperColor core: colour types, wallpaper colour extraction and configuration.
//!
//! Shared by the background service and the terminal client. No terminal or network code here.

pub mod arc;
pub mod color;
pub mod config;
pub mod extract;

pub use color::Rgb;
pub use config::Config;
pub use extract::{ExtractConfig, Extraction, extract, extract_path};
