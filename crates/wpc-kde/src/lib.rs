//! KDE Plasma wallpaper source.
//!
//! `org.kde.PlasmaShell.wallpaper(screen)` returns the wallpaper settings of a screen, and its
//! `Image` key is the picture on screen right now, including the current slide of a slideshow.
//! Plasma signals `wallpaperChanged` only when the settings change (not on every slide), so
//! slide changes must be detected some other way (the service watches the slideshow folders).

use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use zbus::MatchRule;
use zbus::blocking::{Connection, MessageIterator};
use zbus::message::Type;
use zbus::zvariant::OwnedValue;

pub use zbus::Error;

const SERVICE: &str = "org.kde.plasmashell";
const PATH: &str = "/PlasmaShell";
const INTERFACE: &str = "org.kde.PlasmaShell";

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Wallpaper {
    /// Wallpaper plugin, e.g. `org.kde.image` or `org.kde.slideshow`.
    pub plugin: String,
    /// Picture on screen: a file, or a wallpaper package folder (see [`resolve_image`]).
    pub image: Option<PathBuf>,
    /// Folders of a slideshow (empty for other plugins).
    pub slide_paths: Vec<PathBuf>,
}

impl Wallpaper {
    pub fn is_slideshow(&self) -> bool {
        self.plugin == "org.kde.slideshow"
    }
}

pub struct Plasma {
    conn: Connection,
}

impl Plasma {
    pub fn connect() -> zbus::Result<Self> {
        Ok(Self {
            conn: Connection::session()?,
        })
    }

    /// Wallpaper of `screen` (0 = first screen).
    pub fn wallpaper(&self, screen: u32) -> zbus::Result<Wallpaper> {
        let reply = self.conn.call_method(
            Some(SERVICE),
            PATH,
            Some(INTERFACE),
            "wallpaper",
            &(screen,),
        )?;
        let map: HashMap<String, OwnedValue> = reply.body().deserialize()?;
        let string = |key: &str| {
            map.get(key)
                .and_then(|v| String::try_from(v.try_clone().ok()?).ok())
        };
        let slide_paths = map
            .get("SlidePaths")
            .and_then(|v| Vec::<String>::try_from(v.try_clone().ok()?).ok())
            .unwrap_or_default();
        Ok(Wallpaper {
            plugin: string("wallpaperPlugin").unwrap_or_default(),
            image: string("Image").and_then(|s| url_to_path(&s)),
            slide_paths: slide_paths.iter().filter_map(|s| url_to_path(s)).collect(),
        })
    }

    /// Messages for every `wallpaperChanged` signal (settings changed, plugin switched…).
    pub fn wallpaper_changed(&self) -> zbus::Result<MessageIterator> {
        let rule = MatchRule::builder()
            .msg_type(Type::Signal)
            .interface(INTERFACE)?
            .member("wallpaperChanged")?
            .build();
        MessageIterator::for_match_rule(rule, &self.conn, Some(16))
    }

    /// Messages whenever plasmashell starts or stops (its bus name changes owner).
    pub fn plasmashell_restarted(&self) -> zbus::Result<MessageIterator> {
        let rule = MatchRule::builder()
            .msg_type(Type::Signal)
            .sender("org.freedesktop.DBus")?
            .interface("org.freedesktop.DBus")?
            .member("NameOwnerChanged")?
            .arg(0, SERVICE)?
            .build();
        MessageIterator::for_match_rule(rule, &self.conn, Some(16))
    }
}

/// `file:///a/b%20c.png` → `/a/b c.png`; plain paths pass through.
pub fn url_to_path(s: &str) -> Option<PathBuf> {
    if s.is_empty() || s == "null" {
        return None;
    }
    let Some(rest) = s.strip_prefix("file://") else {
        return s.starts_with('/').then(|| PathBuf::from(s));
    };
    let bytes = rest.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            let hex = std::str::from_utf8(&bytes[i + 1..i + 3]).ok();
            if let Some(b) = hex.and_then(|h| u8::from_str_radix(h, 16).ok()) {
                out.push(b);
                i += 3;
                continue;
            }
        }
        out.push(bytes[i]);
        i += 1;
    }
    use std::os::unix::ffi::OsStringExt;
    Some(PathBuf::from(std::ffi::OsString::from_vec(out)))
}

/// The picture to analyse: `path` itself if it is a file, or for a wallpaper package folder its
/// biggest picture in `contents/images` (`contents/images_dark` first when `dark`).
pub fn resolve_image(path: &Path, dark: bool) -> Option<PathBuf> {
    if path.is_file() {
        return Some(path.to_owned());
    }
    let subdirs: &[&str] = if dark {
        &["images_dark", "images"]
    } else {
        &["images"]
    };
    subdirs.iter().find_map(|sub| {
        fs::read_dir(path.join("contents").join(sub))
            .ok()?
            .filter_map(|e| e.ok())
            .filter(|e| e.file_type().is_ok_and(|t| t.is_file()))
            .max_by_key(|e| e.metadata().map(|m| m.len()).unwrap_or(0))
            .map(|e| e.path())
    })
}

/// `~/.config/kdeglobals`.
pub fn kdeglobals_path() -> Option<PathBuf> {
    let base = std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .filter(|p| p.is_absolute())
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))?;
    Some(base.join("kdeglobals"))
}

/// Whether the colour scheme is dark: the window background is darker than mid-grey.
pub fn dark_mode() -> bool {
    kdeglobals_path()
        .and_then(|p| fs::read_to_string(p).ok())
        .is_some_and(|text| is_dark_scheme(&text))
}

fn is_dark_scheme(kdeglobals: &str) -> bool {
    let mut in_window = false;
    for line in kdeglobals.lines() {
        if line.starts_with('[') {
            in_window = line == "[Colors:Window]";
            continue;
        }
        let Some(v) = line.strip_prefix("BackgroundNormal=").filter(|_| in_window) else {
            continue;
        };
        let rgb: Vec<u32> = v.split(',').filter_map(|x| x.trim().parse().ok()).collect();
        if let [r, g, b, ..] = rgb[..] {
            return (299 * r + 587 * g + 114 * b) / 1000 <= 128;
        }
    }
    false
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn urls() {
        assert_eq!(
            url_to_path("file:///a/b.png"),
            Some(PathBuf::from("/a/b.png"))
        );
        assert_eq!(
            url_to_path("file:///a/b%20c.png"),
            Some(PathBuf::from("/a/b c.png"))
        );
        assert_eq!(
            url_to_path("file:///w/%E5%AE%B6.jpg"),
            Some(PathBuf::from("/w/家.jpg"))
        );
        assert_eq!(
            url_to_path("file:///w/家.jpg"),
            Some(PathBuf::from("/w/家.jpg"))
        );
        assert_eq!(
            url_to_path("/plain/path.jpg"),
            Some(PathBuf::from("/plain/path.jpg"))
        );
        assert_eq!(
            url_to_path("file:///a/100%"),
            Some(PathBuf::from("/a/100%"))
        );
        assert_eq!(url_to_path(""), None);
        assert_eq!(url_to_path("null"), None);
        assert_eq!(url_to_path("relative.png"), None);
    }

    #[test]
    fn scheme_brightness() {
        assert!(is_dark_scheme(
            "[General]\nX=1\n[Colors:Window]\nBackgroundNormal=24,24,27\n"
        ));
        assert!(!is_dark_scheme(
            "[Colors:Window]\nBackgroundNormal=239,240,241\n"
        ));
        assert!(!is_dark_scheme("[Colors:View]\nBackgroundNormal=0,0,0\n"));
        assert!(!is_dark_scheme(""));
    }

    #[test]
    fn package_resolution() {
        let dir = std::env::temp_dir().join(format!("wpc-kde-{}", std::process::id()));
        for (sub, name, size) in [
            ("images", "small.jpg", 10),
            ("images", "big.jpg", 100),
            ("images_dark", "dark.jpg", 50),
        ] {
            let d = dir.join("contents").join(sub);
            fs::create_dir_all(&d).unwrap();
            fs::write(d.join(name), vec![0u8; size]).unwrap();
        }
        assert_eq!(
            resolve_image(&dir, false).unwrap().file_name().unwrap(),
            "big.jpg"
        );
        assert_eq!(
            resolve_image(&dir, true).unwrap().file_name().unwrap(),
            "dark.jpg"
        );
        let file = dir.join("contents/images/big.jpg");
        assert_eq!(resolve_image(&file, true).unwrap(), file);
        assert_eq!(resolve_image(&dir.join("missing"), false), None);
        fs::remove_dir_all(&dir).unwrap();
    }
}
