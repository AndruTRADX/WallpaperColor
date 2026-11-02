//! Edits the config file in place, keeping the user's comments and layout (toml_edit).
//! The service notices the file change and reloads it.

use std::path::{Path, PathBuf};
use std::{fs, io};
use toml_edit::{Array, DocumentMut, Item, Table, value};
use wpc_core::Config;
use wpc_core::config::Mode;

pub struct Settings {
    path: PathBuf,
    doc: DocumentMut,
}

impl Settings {
    /// Reads `path`; a missing file starts empty.
    pub fn load(path: &Path) -> Result<Self, Box<dyn std::error::Error>> {
        let text = match fs::read_to_string(path) {
            Ok(t) => t,
            Err(e) if e.kind() == io::ErrorKind::NotFound => String::new(),
            Err(e) => return Err(e.into()),
        };
        Ok(Self {
            path: path.to_owned(),
            doc: text.parse()?,
        })
    }

    /// The config as the service will read it.
    pub fn config(&self) -> Config {
        Config::from_toml(&self.doc.to_string()).unwrap_or_default()
    }

    pub fn set_mode(&mut self, mode: Mode) {
        let name = match mode {
            Mode::Arc => "arc",
            Mode::Single => "single",
        };
        table(&mut self.doc, "led")["mode"] = value(name);
    }

    pub fn set_intensity(&mut self, v: f64) {
        table(&mut self.doc, "extract")["min_saturation"] = value(round2(v.clamp(0.2, 1.0)));
    }

    /// Sets channel `ch` (0 red, 1 green, 2 blue) of calibration entry `i`.
    pub fn set_gain(&mut self, i: usize, ch: usize, v: f64) {
        let Some(entry) = self
            .doc
            .get_mut("calibration")
            .and_then(Item::as_array_of_tables_mut)
            .and_then(|a| a.get_mut(i))
        else {
            return;
        };
        let mut gain: Vec<f64> = entry
            .get("gain")
            .and_then(Item::as_array)
            .map(|a| {
                a.iter()
                    .filter_map(|x| x.as_float().or(x.as_integer().map(|n| n as f64)))
                    .collect()
            })
            .unwrap_or_default();
        gain.resize(3, 1.0);
        gain[ch] = round2(v.clamp(0.0, 1.0));
        let mut array = Array::new();
        for g in gain {
            array.push(g);
        }
        let comment = entry
            .get("gain")
            .and_then(|g| g.as_value())
            .map(|v| v.decor().clone());
        entry["gain"] = value(array);
        if let (Some(decor), Some(v)) = (comment, entry["gain"].as_value_mut()) {
            *v.decor_mut() = decor; // keep a trailing "# red, green, blue"
        }
    }

    /// Writes the file atomically (the service never reads a half-written file).
    pub fn save(&self) -> io::Result<()> {
        if let Some(dir) = self.path.parent() {
            fs::create_dir_all(dir)?;
        }
        let tmp = self.path.with_extension("toml.tmp");
        fs::write(&tmp, self.doc.to_string())?;
        fs::rename(tmp, &self.path)
    }
}

fn table<'a>(doc: &'a mut DocumentMut, key: &str) -> &'a mut Item {
    doc.entry(key).or_insert(Item::Table(Table::new()))
}

fn round2(v: f64) -> f64 {
    (v * 100.0).round() / 100.0
}

#[cfg(test)]
mod tests {
    use super::*;

    const USER: &str = "# my settings\n[led]\nmode = \"arc\"  # gradient\n\n[extract]\nmin_saturation = 0.65   # intensity\n\n# fans have a weak blue\n[[calibration]]\ndevice = \"COMMANDER Core\"\nzone = \"Fan\"\ngain = [0.5, 0.5 ,1.0]    # red, green, blue\n";

    fn settings(text: &str) -> Settings {
        Settings {
            path: PathBuf::from("/nonexistent/config.toml"),
            doc: text.parse().unwrap(),
        }
    }

    #[test]
    fn edits_keep_comments() {
        let mut s = settings(USER);
        s.set_mode(Mode::Single);
        s.set_intensity(0.7);
        s.set_gain(0, 0, 0.55);
        let out = s.doc.to_string();
        for comment in [
            "# my settings",
            "# gradient",
            "# intensity",
            "# fans have a weak blue",
            "# red, green, blue",
        ] {
            assert!(out.contains(comment), "lost {comment:?} in:\n{out}");
        }
        let c = s.config();
        assert_eq!(c.led.mode, Mode::Single);
        assert_eq!(c.extract.min_saturation, 0.7);
        assert_eq!(c.calibration[0].gain, [0.55, 0.5, 1.0]);
    }

    #[test]
    fn empty_file_gets_the_tables() {
        let mut s = settings("");
        s.set_mode(Mode::Arc);
        s.set_intensity(0.333);
        let c = s.config();
        assert_eq!(c.led.mode, Mode::Arc);
        assert_eq!(c.extract.min_saturation, 0.33);
    }

    #[test]
    fn values_are_clamped() {
        let mut s = settings(USER);
        s.set_intensity(5.0);
        s.set_gain(0, 2, 1.7);
        s.set_gain(7, 0, 0.1); // no such entry: ignored
        let c = s.config();
        assert_eq!(c.extract.min_saturation, 1.0);
        assert_eq!(c.calibration[0].gain[2], 1.0);
        assert_eq!(c.calibration.len(), 1);
    }
}
