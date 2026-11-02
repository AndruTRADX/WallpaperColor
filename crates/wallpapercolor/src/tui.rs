//! Terminal UI: what the service is doing, a preview of every device, and the settings.
//!
//! Settings are saved to the config file as they change (the service reloads it within about
//! a second); the preview is computed here at once from the same picture, so changes show
//! immediately. Closing the UI leaves the service running.

use crate::settings::Settings;
use ratatui::DefaultTerminal;
use ratatui::Frame;
use ratatui::crossterm::event::{self, Event, KeyCode, KeyEventKind};
use ratatui::layout::{Constraint, Layout, Rect};
use ratatui::style::{Color, Modifier, Style, Stylize};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Paragraph};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};
use wpc_core::arc::gradient;
use wpc_core::config::Mode;
use wpc_core::ipc::{self, Request, Response, Status};
use wpc_core::{Config, Rgb, extract_path};

const REFRESH: Duration = Duration::from_secs(1);
const DRAM: i32 = 1;

#[derive(Clone, Copy, PartialEq)]
enum Item {
    Mode,
    Intensity,
    /// Calibration entry, channel.
    Gain(usize, usize),
}

struct App {
    settings: Settings,
    config: Config,
    status: Result<Status, String>,
    /// Picture the local preview was computed from, and its stops.
    preview: Option<(PathBuf, Vec<Rgb>)>,
    selected: usize,
    message: String,
    quit: bool,
}

pub fn run(config_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let settings = Settings::load(config_path)?;
    let mut app = App {
        config: settings.config(),
        settings,
        status: Err(String::new()),
        preview: None,
        selected: 0,
        message: String::new(),
        quit: false,
    };
    ratatui::run(|terminal| app.run(terminal))?;
    Ok(())
}

impl App {
    fn run(&mut self, terminal: &mut DefaultTerminal) -> std::io::Result<()> {
        let mut next_refresh = Instant::now();
        while !self.quit {
            if Instant::now() >= next_refresh {
                self.refresh();
                next_refresh = Instant::now() + REFRESH;
            }
            terminal.draw(|f| self.draw(f))?;
            if event::poll(next_refresh.saturating_duration_since(Instant::now()))?
                && let Event::Key(key) = event::read()?
                && key.kind == KeyEventKind::Press
            {
                self.key(key.code);
                next_refresh = Instant::now(); // show the effect of the key at once
            }
        }
        Ok(())
    }

    fn refresh(&mut self) {
        self.status = match ipc::call(&Request::Status) {
            Ok(Response::Status(s)) => Ok(s),
            Ok(other) => Err(format!("unexpected answer: {other:?}")),
            Err(e) => Err(format!(
                "service not running ({e}): systemctl --user start wallpapercolord"
            )),
        };
        let image = self.status.as_ref().ok().and_then(|s| s.image.clone());
        if let Some(image) = image
            && self.preview.as_ref().is_none_or(|(p, _)| p != &image)
        {
            self.recompute(image);
        }
    }

    /// Local preview with the settings being edited.
    fn recompute(&mut self, image: PathBuf) {
        let stops = extract_path(&image, &self.config.extract)
            .map(|e| e.stops(self.config.led.mode))
            .unwrap_or_default();
        self.preview = Some((image, stops));
    }

    fn items(&self) -> Vec<Item> {
        let mut items = vec![Item::Mode, Item::Intensity];
        for i in 0..self.config.calibration.len() {
            items.extend((0..3).map(|ch| Item::Gain(i, ch)));
        }
        items
    }

    fn key(&mut self, code: KeyCode) {
        let items = self.items();
        match code {
            KeyCode::Char('q') | KeyCode::Esc => self.quit = true,
            KeyCode::Up | KeyCode::Char('k') => self.selected = self.selected.saturating_sub(1),
            KeyCode::Down | KeyCode::Char('j') => {
                self.selected = (self.selected + 1).min(items.len() - 1)
            }
            KeyCode::Left | KeyCode::Char('h') => self.adjust(items[self.selected], -1.0),
            KeyCode::Right | KeyCode::Char('l') => self.adjust(items[self.selected], 1.0),
            KeyCode::Char('p') => {
                let paused = self.status.as_ref().is_ok_and(|s| s.paused);
                let request = if paused {
                    Request::Resume
                } else {
                    Request::Pause
                };
                self.message = match ipc::call(&request) {
                    Ok(_) if paused => "resumed".into(),
                    Ok(_) => "paused: colours are not sent until you resume".into(),
                    Err(e) => format!("service not reachable: {e}"),
                };
            }
            KeyCode::Char('a') => {
                self.message = match ipc::call(&Request::Apply) {
                    Ok(_) => "colours sent again".into(),
                    Err(e) => format!("service not reachable: {e}"),
                };
            }
            _ => {}
        }
    }

    fn adjust(&mut self, item: Item, dir: f64) {
        match item {
            Item::Mode => self.settings.set_mode(match self.config.led.mode {
                Mode::Arc => Mode::Single,
                Mode::Single => Mode::Arc,
            }),
            Item::Intensity => self
                .settings
                .set_intensity(self.config.extract.min_saturation + 0.05 * dir),
            Item::Gain(i, ch) => {
                let v = self.config.calibration[i].gain[ch];
                self.settings.set_gain(i, ch, v + 0.05 * dir);
            }
        }
        self.config = self.settings.config();
        self.message = match self.settings.save() {
            Ok(()) => "saved; the service applies it in about a second".into(),
            Err(e) => format!("cannot save the config: {e}"),
        };
        if let Some((image, _)) = self.preview.take() {
            self.recompute(image);
        }
    }

    fn draw(&self, f: &mut Frame) {
        let devices = self
            .status
            .as_ref()
            .map(|s| s.devices.as_slice())
            .unwrap_or(&[]);
        let device_rows: u16 = devices
            .iter()
            .map(|d| 1 + d.zones.len() as u16)
            .sum::<u16>()
            .max(1);
        let setting_rows = self.items().len() as u16 + self.config.calibration.len() as u16;
        let [header, devices_area, settings_area, footer] = Layout::vertical([
            Constraint::Length(6),
            Constraint::Length(device_rows + 2),
            Constraint::Length(setting_rows + 2),
            Constraint::Min(2),
        ])
        .areas(f.area());

        self.draw_header(f, header);
        self.draw_devices(f, devices_area);
        self.draw_settings(f, settings_area);
        let help = Line::from(vec![
            key("↑↓"),
            " select  ".into(),
            key("←→"),
            " change  ".into(),
            key("p"),
            " pause/resume  ".into(),
            key("a"),
            " apply  ".into(),
            key("q"),
            " quit".into(),
        ]);
        f.render_widget(
            Paragraph::new(vec![Line::from(self.message.clone().italic()), help]),
            footer,
        );
    }

    fn draw_header(&self, f: &mut Frame, area: Rect) {
        let mut lines = Vec::new();
        match &self.status {
            Ok(s) => {
                let state = if s.paused {
                    Span::styled("⏸ paused", Style::new().yellow().bold())
                } else {
                    Span::styled("● running", Style::new().green().bold())
                };
                lines.push(Line::from(vec![
                    state,
                    format!("  v{}  ·  {}", s.version, s.plugin).dim(),
                ]));
                let name = s
                    .image
                    .as_ref()
                    .and_then(|p| p.file_name())
                    .map(|n| n.to_string_lossy().into_owned());
                lines.push(Line::from(vec![
                    "Wallpaper  ".bold(),
                    name.unwrap_or_else(|| "—".into()).into(),
                ]));
                if let Some(e) = &s.error {
                    lines.push(Line::from(Span::styled(
                        format!("⚠ {e}"),
                        Style::new().red(),
                    )));
                }
            }
            Err(e) => lines.push(Line::from(Span::styled(
                format!("✗ {e}"),
                Style::new().red().bold(),
            ))),
        }
        let stops = self.stops();
        let mut colours = vec!["Colours    ".bold()];
        for c in &stops {
            colours.push(Span::styled("      ", Style::new().bg(rgb(*c))));
            colours.push(format!(" {c}  ").into());
        }
        lines.push(Line::from(colours));
        f.render_widget(
            Paragraph::new(lines).block(Block::bordered().title(" WallpaperColor ")),
            area,
        );
    }

    fn draw_devices(&self, f: &mut Frame, area: Rect) {
        let stops = self.stops();
        let mut lines = Vec::new();
        let devices = self
            .status
            .as_ref()
            .map(|s| s.devices.clone())
            .unwrap_or_default();
        if devices.is_empty() {
            lines.push(Line::from(
                "no devices yet: in OpenLinkHub, turn on \"OpenRGB Integration\"".dim(),
            ));
        }
        for d in &devices {
            lines.push(Line::from(d.name.clone().bold()));
            for z in &d.zones {
                let gain = self.config.gain(&d.name, &z.name);
                let leds = gradient(&stops, z.leds as usize, d.kind != DRAM);
                let mut spans = vec![Span::raw(format!("  {:<26.26} {:>3} ", z.name, z.leds))];
                spans.extend(leds.iter().map(|c| {
                    let shown = scale(*c, gain);
                    Span::styled("██", Style::new().fg(rgb(shown)))
                }));
                if gain != [1.0; 3] {
                    spans.push(" calibrated".dim());
                }
                lines.push(Line::from(spans));
            }
        }
        f.render_widget(
            Paragraph::new(lines).block(Block::bordered().title(" Devices ")),
            area,
        );
    }

    fn draw_settings(&self, f: &mut Frame, area: Rect) {
        let mut lines = Vec::new();
        for (n, item) in self.items().iter().enumerate() {
            if let Item::Gain(i, 0) = item {
                let c = &self.config.calibration[*i];
                let zone = c
                    .zone
                    .as_deref()
                    .map(|z| format!(" · {z}"))
                    .unwrap_or_default();
                lines.push(Line::from(format!("Calibration: {}{zone}", c.device).dim()));
            }
            let (label, val) = match item {
                Item::Mode => (
                    "Mode".to_string(),
                    match self.config.led.mode {
                        Mode::Arc => "‹ arc ›  three colours as a gradient".into(),
                        Mode::Single => "‹ single ›  one colour everywhere".into(),
                    },
                ),
                Item::Intensity => {
                    let v = self.config.extract.min_saturation;
                    ("Intensity".into(), format!("{} {v:.2}", bar(v)))
                }
                Item::Gain(i, ch) => {
                    let v = self.config.calibration[*i].gain[*ch];
                    (
                        format!("  {}", ["Red", "Green", "Blue"][*ch]),
                        format!("{} {v:.2}", bar(v)),
                    )
                }
            };
            let style = if n == self.selected {
                Style::new().add_modifier(Modifier::REVERSED)
            } else {
                Style::new()
            };
            lines.push(Line::from(vec![
                Span::styled(format!(" {label:<12} "), style),
                format!(" {val}").into(),
            ]));
        }
        f.render_widget(
            Paragraph::new(lines).block(Block::bordered().title(" Settings ")),
            area,
        );
    }

    fn stops(&self) -> Vec<Rgb> {
        match &self.preview {
            Some((_, stops)) if !stops.is_empty() => stops.clone(),
            _ => self
                .status
                .as_ref()
                .map(|s| s.stops.iter().filter_map(|h| h.parse().ok()).collect())
                .unwrap_or_default(),
        }
    }
}

fn key(k: &str) -> Span<'static> {
    Span::styled(k.to_string(), Style::new().fg(Color::Cyan).bold())
}

fn bar(v: f64) -> String {
    let filled = (v.clamp(0.0, 1.0) * 20.0).round() as usize;
    format!("{}{}", "█".repeat(filled), "░".repeat(20 - filled))
}

fn rgb(c: Rgb) -> Color {
    Color::Rgb(c.r, c.g, c.b)
}

fn scale(c: Rgb, [r, g, b]: [f32; 3]) -> Rgb {
    let s = |v: u8, k: f32| (v as f32 * k).round().clamp(0.0, 255.0) as u8;
    Rgb::new(s(c.r, r), s(c.g, g), s(c.b, b))
}
