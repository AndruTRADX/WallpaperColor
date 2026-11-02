//! `wallpapercolord`: paints every exposed RGB device with the wallpaper on screen.
//!
//! One event loop on the main thread, fed by three sleeping helper threads:
//! - D-Bus: Plasma's `wallpaperChanged` signal and plasmashell (re)starts;
//! - a Unix socket for the terminal client (status, pause, resume, apply);
//! - inotify: files opened in the slideshow folders (plasmashell opens the next slide), and
//!   changes to the desktop settings (`plasma-org.kde.plasma.desktop-appletsrc`), `kdeglobals`
//!   (light/dark wallpaper variants) and our config file.
//!
//! Events only *trigger* a check: the picture on screen always comes from Plasma over D-Bus,
//! so a stray file access cannot paint the wrong colour (same picture → nothing is sent).
//! Bursts are coalesced; nothing runs between events except retries while OpenLinkHub or
//! plasmashell is unreachable.

use inotify::{EventMask, Inotify, WatchDescriptor, WatchMask, Watches};
use std::collections::HashMap;
use std::ffi::OsString;
use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixListener;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::time::{Duration, Instant, SystemTime};
use std::{fs, thread};
use wpc_core::ipc::{self, DeviceInfo, Request, Response, Status, ZoneInfo};
use wpc_core::{Config, extract_path};
use wpc_kde::Plasma;
use wpc_openrgb::Client;

/// Quiet time that ends a burst of events (a slideshow folder rescan opens every file).
const SETTLE: Duration = Duration::from_millis(800);
const RETRY_MIN: Duration = Duration::from_secs(2);
const RETRY_MAX: Duration = Duration::from_secs(60);
/// Where plasmashell saves the desktop settings, wallpaper included.
const APPLETSRC: &str = "plasma-org.kde.plasma.desktop-appletsrc";
const IMAGE_EXT: &[&str] = &["png", "jpg", "jpeg", "webp", "gif", "bmp"];

enum Event {
    /// Plasma's wallpaper settings changed or plasmashell (re)started.
    Plasma,
    /// inotify event: watch descriptor and file name.
    File(WatchDescriptor, Option<OsString>, EventMask),
    /// A client request and where to send the answer.
    Ipc(Request, Sender<Response>),
}

/// What a burst of events asks for.
#[derive(Default)]
struct Todo {
    /// The picture may have changed.
    check: bool,
    /// Re-read the config.
    reload: bool,
    /// Send even if nothing changed.
    force: bool,
}

macro_rules! log {
    ($($arg:tt)*) => { eprintln!($($arg)*) };
}

fn main() {
    let config_path = std::env::args_os()
        .nth(1)
        .map(PathBuf::from)
        .or_else(Config::default_path);
    let mut d = match Daemon::new(config_path) {
        Ok(d) => d,
        Err(e) => {
            log!("wallpapercolord: {e}");
            std::process::exit(1);
        }
    };
    d.run();
}

struct Daemon {
    config_path: Option<PathBuf>,
    config: Config,
    plasma: Plasma,
    rx: Receiver<Event>,
    watches: Watches,
    /// Slideshow folders being watched.
    slide_dirs: HashMap<WatchDescriptor, PathBuf>,
    /// `~/.config` (for kdeglobals) and the folder of our config file.
    config_dirs: HashMap<WatchDescriptor, PathBuf>,
    /// What was last sent: picture, its mtime and the settings used.
    last: Option<String>,
    retry: Option<(Instant, Duration)>,
    paused: bool,
    /// What clients see.
    status: Status,
}

impl Daemon {
    fn new(config_path: Option<PathBuf>) -> Result<Self, Box<dyn std::error::Error>> {
        let config = load_config(config_path.as_deref());
        let plasma = Plasma::connect().map_err(|e| format!("cannot reach the session bus: {e}"))?;
        let (tx, rx) = mpsc::channel();

        spawn_dbus_listener(&plasma, &tx)?;
        spawn_ipc_listener(tx.clone())?;
        let inotify = Inotify::init()?;
        let mut watches = inotify.watches();
        spawn_inotify_reader(inotify, tx);

        let mut config_dirs = HashMap::new();
        let dirs = [
            wpc_kde::kdeglobals_path().and_then(|p| p.parent().map(Path::to_owned)),
            config_path
                .as_deref()
                .and_then(Path::parent)
                .map(Path::to_owned),
        ];
        for dir in dirs.into_iter().flatten() {
            if config_dirs.values().any(|d| d == &dir) {
                continue;
            }
            match watches.add(
                &dir,
                WatchMask::CLOSE_WRITE | WatchMask::MOVED_TO | WatchMask::CREATE,
            ) {
                Ok(wd) => {
                    config_dirs.insert(wd, dir);
                }
                Err(e) => log!("not watching {}: {e}", dir.display()),
            }
        }

        log!(
            "wallpapercolord {}: mode {:?}, sending to {}",
            env!("CARGO_PKG_VERSION"),
            config.led.mode,
            config.openrgb.address
        );
        Ok(Self {
            config_path,
            config,
            plasma,
            rx,
            watches,
            slide_dirs: HashMap::new(),
            config_dirs,
            last: None,
            retry: None,
            paused: false,
            status: Status {
                version: env!("CARGO_PKG_VERSION").into(),
                ..Default::default()
            },
        })
    }

    fn run(&mut self) -> ! {
        self.sync(false);
        loop {
            let event = match self.retry {
                Some((at, _)) => match self
                    .rx
                    .recv_timeout(at.saturating_duration_since(Instant::now()))
                {
                    Ok(e) => Some(e),
                    Err(RecvTimeoutError::Timeout) => None,
                    Err(RecvTimeoutError::Disconnected) => panic!("event threads stopped"),
                },
                None => Some(self.rx.recv().expect("event threads stopped")),
            };

            let mut todo = Todo {
                check: event.is_none(), // retry time
                ..Default::default()
            };
            if let Some(e) = event {
                self.classify(e, &mut todo);
            }
            if !todo.check && !todo.reload && !todo.force {
                continue;
            }
            // coalesce the rest of the burst (client requests are still answered at once)
            if !todo.force {
                while let Ok(e) = self.rx.recv_timeout(SETTLE) {
                    self.classify(e, &mut todo);
                }
            }
            if todo.reload {
                self.config = load_config(self.config_path.as_deref());
                log!("config reloaded: mode {:?}", self.config.led.mode);
            }
            self.sync(todo.reload || todo.force);
        }
    }

    /// Decides what an event asks for; answers client requests right away.
    fn classify(&mut self, e: Event, todo: &mut Todo) {
        match e {
            Event::Plasma => todo.check = true,
            Event::Ipc(request, reply) => {
                let response = match request {
                    Request::Status => Response::Status(self.status()),
                    Request::Pause => {
                        self.paused = true;
                        log!("paused");
                        Response::Ok
                    }
                    Request::Resume => {
                        self.paused = false;
                        todo.force = true;
                        log!("resumed");
                        Response::Ok
                    }
                    Request::Apply => {
                        todo.force = true;
                        Response::Ok
                    }
                };
                let _ = reply.send(response);
            }
            Event::File(wd, name, mask) => {
                let name = name.unwrap_or_default();
                if self.slide_dirs.contains_key(&wd) {
                    let is_image = Path::new(&name)
                        .extension()
                        .and_then(|x| x.to_str())
                        .is_some_and(|x| IMAGE_EXT.contains(&x.to_ascii_lowercase().as_str()));
                    if is_image
                        || (mask.contains(EventMask::CREATE) && mask.contains(EventMask::ISDIR))
                    {
                        todo.check = true;
                    }
                } else if let Some(dir) = self.config_dirs.get(&wd).cloned() {
                    // kdeglobals: a light/dark switch picks another wallpaper package variant.
                    // appletsrc: wallpaper changed in the settings, which does not always emit
                    // wallpaperChanged (e.g. switching from a slideshow to a single picture).
                    if name == "kdeglobals" || name == APPLETSRC {
                        todo.check = true;
                    }
                    let config_dir = self.config_path.as_deref().and_then(Path::parent);
                    let ours = self.config_path.as_deref().and_then(Path::file_name);
                    if config_dir == Some(dir.as_path()) && ours.is_some_and(|f| f == name) {
                        todo.reload = true;
                    }
                    // our config folder was created after we started: watch it now
                    if let Some(cd) = config_dir.filter(|cd| cd.parent() == Some(dir.as_path()))
                        && cd.file_name() == Some(name.as_os_str())
                        && !self.config_dirs.values().any(|d| d == cd)
                    {
                        let cd = cd.to_owned();
                        let mask = WatchMask::CLOSE_WRITE | WatchMask::MOVED_TO | WatchMask::CREATE;
                        if let Ok(wd) = self.watches.add(&cd, mask) {
                            self.config_dirs.insert(wd, cd);
                            todo.reload = true;
                        }
                    }
                }
            }
        }
    }

    fn status(&self) -> Status {
        Status {
            paused: self.paused,
            mode: self.config.led.mode,
            ..self.status.clone()
        }
    }

    /// Asks Plasma for the picture on screen and sends its colours if anything changed.
    fn sync(&mut self, force: bool) {
        let wallpaper = match self.plasma.wallpaper(0) {
            Ok(w) => w,
            Err(e) => return self.fail(format!("cannot ask Plasma for the wallpaper: {e}")),
        };
        self.status.plugin = wallpaper.plugin.clone();
        self.watch_slides(if wallpaper.is_slideshow() {
            &wallpaper.slide_paths
        } else {
            &[]
        });

        let Some(image) = wallpaper
            .image
            .as_deref()
            .and_then(|p| wpc_kde::resolve_image(p, wpc_kde::dark_mode()))
        else {
            self.retry = None;
            return log!(
                "no picture to take colours from (plugin {})",
                wallpaper.plugin
            );
        };

        self.status.image = Some(image.clone());
        let mtime = fs::metadata(&image).and_then(|m| m.modified()).ok();
        let key = format!(
            "{} {mtime:?} {:?} {:?}",
            image.display(),
            self.config.led,
            self.config.extract
        );
        if !force && self.retry.is_none() && self.last.as_deref() == Some(key.as_str()) {
            return;
        }

        let stops = match extract_path(&image, &self.config.extract) {
            Ok(e) => e.stops(self.config.led.mode),
            Err(e) => {
                self.retry = None;
                self.status.error = Some(format!("{}: {e}", image.display()));
                return log!("{}: {e}", image.display());
            }
        };
        let colours: Vec<String> = stops.iter().map(ToString::to_string).collect();
        self.status.stops = colours.clone();
        if self.paused {
            self.last = None; // send on resume
            return;
        }

        match self.send(&stops) {
            Ok(devices) => {
                log!(
                    "{} → {} device(s) from {}",
                    colours.join(" "),
                    devices.len(),
                    image.display()
                );
                self.status.devices = devices;
                self.status.error = None;
                self.status.last_sent = SystemTime::now()
                    .duration_since(SystemTime::UNIX_EPOCH)
                    .ok()
                    .map(|d| d.as_secs());
                self.last = Some(key);
                self.retry = None;
            }
            Err(e) => self.fail(format!(
                "cannot reach the OpenRGB server at {}: {e}",
                self.config.openrgb.address
            )),
        }
    }

    fn send(&self, stops: &[wpc_core::Rgb]) -> wpc_openrgb::Result<Vec<DeviceInfo>> {
        let mut client = Client::connect(
            self.config.openrgb.address.as_str(),
            &self.config.openrgb.client_name,
        )?;
        let ctrls = client.controllers()?;
        for c in &ctrls {
            let leds = c.paint_calibrated(stops, |zone| self.config.gain(&c.name, zone));
            client.set_leds(c.index, &leds)?;
        }
        Ok(ctrls
            .iter()
            .map(|c| DeviceInfo {
                name: c.name.clone(),
                kind: c.kind,
                zones: c
                    .zones
                    .iter()
                    .map(|z| ZoneInfo {
                        name: z.name.clone(),
                        leds: z.leds,
                    })
                    .collect(),
            })
            .collect())
    }

    /// Logs and schedules a retry with exponential backoff.
    fn fail(&mut self, msg: String) {
        let delay = self
            .retry
            .map_or(RETRY_MIN, |(_, d)| (d * 2).min(RETRY_MAX));
        log!("{msg} (retrying in {}s)", delay.as_secs());
        self.status.error = Some(msg);
        self.retry = Some((Instant::now() + delay, delay));
    }

    /// Watches exactly `dirs` and their subfolders (Plasma slideshows are recursive).
    fn watch_slides(&mut self, dirs: &[PathBuf]) {
        let wanted: Vec<PathBuf> = dirs.iter().flat_map(|d| subdirs(d)).collect();
        let current: Vec<&PathBuf> = self.slide_dirs.values().collect();
        if wanted.len() == current.len() && wanted.iter().all(|w| current.contains(&w)) {
            return;
        }
        for wd in self.slide_dirs.keys().cloned().collect::<Vec<_>>() {
            let _ = self.watches.remove(wd);
        }
        self.slide_dirs.clear();
        for dir in wanted {
            match self.watches.add(&dir, WatchMask::OPEN | WatchMask::CREATE) {
                Ok(wd) => {
                    self.slide_dirs.insert(wd, dir);
                }
                Err(e) => log!("not watching {}: {e}", dir.display()),
            }
        }
        if !self.slide_dirs.is_empty() {
            log!("watching {} slideshow folder(s)", self.slide_dirs.len());
        }
    }
}

fn load_config(path: Option<&Path>) -> Config {
    match path.map(Config::load) {
        Some(Ok(c)) => c,
        Some(Err(e)) => {
            log!("{e}; using the defaults");
            Config::default()
        }
        None => Config::default(),
    }
}

/// `dir` and every folder below it.
fn subdirs(dir: &Path) -> Vec<PathBuf> {
    let mut out = vec![dir.to_owned()];
    let mut i = 0;
    while i < out.len() {
        if let Ok(entries) = fs::read_dir(&out[i]) {
            for e in entries.flatten() {
                if e.file_type().is_ok_and(|t| t.is_dir()) {
                    out.push(e.path());
                }
            }
        }
        i += 1;
    }
    out
}

fn spawn_dbus_listener(plasma: &Plasma, tx: &Sender<Event>) -> Result<(), wpc_kde::Error> {
    for messages in [plasma.wallpaper_changed()?, plasma.plasmashell_restarted()?] {
        let tx = tx.clone();
        thread::spawn(move || {
            for msg in messages {
                if msg.is_ok() && tx.send(Event::Plasma).is_err() {
                    return;
                }
            }
        });
    }
    Ok(())
}

/// Answers clients on the socket: one JSON request line in, one JSON response line out.
fn spawn_ipc_listener(tx: Sender<Event>) -> std::io::Result<()> {
    let path = ipc::socket_path();
    let _ = fs::remove_file(&path); // stale socket of a previous run
    let listener = UnixListener::bind(&path)?;
    thread::spawn(move || {
        for stream in listener.incoming().flatten() {
            let _ = stream.set_read_timeout(Some(Duration::from_secs(2)));
            let mut line = String::new();
            let Ok(reader) = stream.try_clone() else {
                continue;
            };
            if BufReader::new(reader).read_line(&mut line).is_err() {
                continue;
            }
            let response = match serde_json::from_str::<Request>(&line) {
                Ok(request) => {
                    let (reply_tx, reply_rx) = mpsc::channel();
                    if tx.send(Event::Ipc(request, reply_tx)).is_err() {
                        return;
                    }
                    reply_rx
                        .recv_timeout(Duration::from_secs(2))
                        .unwrap_or(Response::Error {
                            message: "service busy".into(),
                        })
                }
                Err(e) => Response::Error {
                    message: format!("bad request: {e}"),
                },
            };
            let mut out = serde_json::to_string(&response).unwrap_or_default();
            out.push('\n');
            let mut stream = stream;
            let _ = stream.write_all(out.as_bytes());
        }
    });
    Ok(())
}

fn spawn_inotify_reader(mut inotify: Inotify, tx: Sender<Event>) {
    thread::spawn(move || {
        let mut buffer = [0u8; 4096];
        loop {
            let events = match inotify.read_events_blocking(&mut buffer) {
                Ok(e) => e,
                Err(e) => {
                    log!("inotify: {e}");
                    return;
                }
            };
            for e in events {
                if tx
                    .send(Event::File(e.wd, e.name.map(OsString::from), e.mask))
                    .is_err()
                {
                    return;
                }
            }
        }
    });
}
