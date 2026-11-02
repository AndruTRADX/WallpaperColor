//! `wallpapercolor`: terminal client for WallpaperColor. Without a subcommand: the terminal UI.

mod settings;
mod tui;

use clap::{Args, Parser, Subcommand};
use std::io::IsTerminal;
use std::path::PathBuf;
use std::process::ExitCode;
use wpc_core::config::Mode;
use wpc_core::ipc;
use wpc_core::{Config, Rgb, extract_path};
use wpc_openrgb::Client;

/// `println!` that exits quietly when the reader went away (`wallpapercolor ... | head`)
/// instead of panicking on a broken pipe.
macro_rules! out {
    ($($arg:tt)*) => {{
        use std::io::Write as _;
        if let Err(e) = writeln!(std::io::stdout().lock(), $($arg)*) {
            if e.kind() == std::io::ErrorKind::BrokenPipe {
                std::process::exit(0);
            }
            panic!("cannot write to stdout: {e}");
        }
    }};
}

#[derive(Parser)]
#[command(version, about = "Paint your RGB hardware with the wallpaper colour")]
struct Cli {
    /// Config file (default: ~/.config/wallpapercolor/config.toml)
    #[arg(long, global = true)]
    config: Option<PathBuf>,

    #[command(subcommand)]
    command: Option<Command>,
}

#[derive(Subcommand)]
enum Command {
    /// Show the LED colour that would be taken from one or more pictures
    Extract {
        /// Pictures to analyse
        #[arg(required = true)]
        images: Vec<PathBuf>,
        /// Also show the palette the colour was picked from
        #[arg(short, long)]
        palette: bool,
    },
    /// List the RGB devices the server exposes (in OpenLinkHub: "OpenRGB Integration" on)
    Devices,
    /// Paint every exposed device with a colour, or three as an arc
    Set(SetArgs),
    /// Print the effective configuration (defaults included) as TOML
    Config,
    /// Show what the service is doing
    Status,
    /// Stop sending colours until resume (the service keeps running)
    Pause,
    /// Start sending colours again
    Resume,
    /// Send the current wallpaper's colours again now
    Apply,
    /// Open the terminal UI (the default)
    Tui,
}

#[derive(Args)]
struct SetArgs {
    /// Colour(s) as #rrggbb or rrggbb; up to three with --arc
    #[arg(required_unless_present = "image", conflicts_with = "image", num_args = 1..=3)]
    colors: Vec<Rgb>,
    /// Take the colour(s) from this picture instead
    #[arg(long)]
    image: Option<PathBuf>,
    /// Spread three colours as a gradient: around each ring (fans, pump), top to bottom on RAM
    /// (default when the config says mode = "arc")
    #[arg(long, conflicts_with = "single")]
    arc: bool,
    /// One colour on every LED (default when the config says mode = "single")
    #[arg(long)]
    single: bool,
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    match run(cli) {
        Ok(code) => code,
        Err(e) => {
            eprintln!("wallpapercolor: {e}");
            ExitCode::FAILURE
        }
    }
}

type Error = Box<dyn std::error::Error>;

fn run(cli: Cli) -> Result<ExitCode, Error> {
    let config_path = cli.config.clone().or_else(Config::default_path);
    let config = match &config_path {
        Some(path) => Config::load(path)?,
        None => Config::default(),
    };

    let Some(command) = cli.command else {
        return open_tui(config_path);
    };
    match command {
        Command::Tui => open_tui(config_path),
        Command::Status => {
            let s = match ipc::call(&ipc::Request::Status).map_err(service_down)? {
                ipc::Response::Status(s) => s,
                other => return Err(format!("unexpected answer: {other:?}").into()),
            };
            out!(
                "{} v{}  mode {:?}",
                if s.paused { "paused" } else { "running" },
                s.version,
                s.mode
            );
            let image = s.image.as_ref().map(|p| p.display().to_string());
            out!(
                "wallpaper  {} ({})",
                image.unwrap_or_else(|| "—".into()),
                s.plugin
            );
            let stops: Vec<String> = s
                .stops
                .iter()
                .map(|h| match h.parse::<Rgb>() {
                    Ok(c) => format!("{}{h}", swatch(c, 2)),
                    Err(_) => h.clone(),
                })
                .collect();
            out!("colours    {}", stops.join(" "));
            for d in &s.devices {
                let zones: Vec<String> = d
                    .zones
                    .iter()
                    .map(|z| format!("{} ({})", z.name, z.leds))
                    .collect();
                out!("device     {}: {}", d.name, zones.join(", "));
            }
            if let Some(e) = &s.error {
                out!("problem    {e}");
            }
            Ok(ExitCode::SUCCESS)
        }
        Command::Pause | Command::Resume | Command::Apply => {
            let (request, done) = match command {
                Command::Pause => (ipc::Request::Pause, "paused"),
                Command::Resume => (ipc::Request::Resume, "resumed"),
                _ => (ipc::Request::Apply, "colours sent again"),
            };
            match ipc::call(&request).map_err(service_down)? {
                ipc::Response::Error { message } => Err(message.into()),
                _ => {
                    out!("{done}");
                    Ok(ExitCode::SUCCESS)
                }
            }
        }
        Command::Extract { images, palette } => {
            let mut failed = false;
            for path in &images {
                match extract_path(path, &config.extract) {
                    Ok(e) => {
                        let arc: String = e.arc.iter().map(|c| swatch(*c, 2)).collect();
                        out!(
                            "{}{}  arc {}{} {} {}  {}",
                            swatch(e.led, 6),
                            e.led,
                            arc,
                            e.arc[0],
                            e.arc[1],
                            e.arc[2],
                            path.display()
                        );
                        if palette {
                            for (c, share) in &e.palette {
                                let mark = if Some(*c) == e.picked { "←" } else { "" };
                                out!("    {}{c} {:5.1}% {mark}", swatch(*c, 2), share * 100.0);
                            }
                        }
                    }
                    Err(err) => {
                        eprintln!("{}: {err}", path.display());
                        failed = true;
                    }
                }
            }
            Ok(if failed {
                ExitCode::FAILURE
            } else {
                ExitCode::SUCCESS
            })
        }
        Command::Devices => {
            let mut client = connect(&config)?;
            let ctrls = client.controllers()?;
            out!(
                "{} (protocol {}): {} device(s)",
                config.openrgb.address,
                client.protocol(),
                ctrls.len()
            );
            if ctrls.is_empty() {
                out!(
                    "  none exposed: in OpenLinkHub, turn on \"OpenRGB Integration\" for a device"
                );
            }
            for c in &ctrls {
                let now = c.colors.first().copied().unwrap_or_default();
                out!(
                    "  [{}] {}{}  {} LEDs  now {now}",
                    c.index,
                    swatch(now, 2),
                    c.name,
                    c.led_count()
                );
                for z in &c.zones {
                    out!("        {}: {} LEDs", z.name, z.leds);
                }
            }
            Ok(ExitCode::SUCCESS)
        }
        Command::Set(args) => {
            let mode = match (args.arc, args.single) {
                (true, _) => Mode::Arc,
                (_, true) => Mode::Single,
                _ => config.led.mode,
            };
            let stops: Vec<Rgb> = match &args.image {
                Some(path) => extract_path(path, &config.extract)?.stops(mode),
                None if mode == Mode::Arc => args.colors.clone(),
                None => args.colors[..1].to_vec(),
            };
            let mut client = connect(&config)?;
            let ctrls = client.controllers()?;
            if ctrls.is_empty() {
                return Err(
                    "no devices exposed: turn on \"OpenRGB Integration\" in OpenLinkHub".into(),
                );
            }
            let label: Vec<String> = stops
                .iter()
                .map(|c| format!("{}{c}", swatch(*c, 2)))
                .collect();
            for c in &ctrls {
                let leds = c.paint_calibrated(&stops, |zone| config.gain(&c.name, zone));
                client.set_leds(c.index, &leds)?;
                out!("{} → [{}] {}", label.join(" "), c.index, c.name);
            }
            Ok(ExitCode::SUCCESS)
        }
        Command::Config => {
            out!("{}", config.to_toml().trim_end());
            Ok(ExitCode::SUCCESS)
        }
    }
}

fn open_tui(config_path: Option<std::path::PathBuf>) -> Result<ExitCode, Error> {
    let path = config_path.ok_or("no config path: set HOME or XDG_CONFIG_HOME")?;
    tui::run(&path)?;
    Ok(ExitCode::SUCCESS)
}

fn service_down(e: std::io::Error) -> Error {
    format!("the service is not running ({e}): systemctl --user start wallpapercolord").into()
}

fn connect(config: &Config) -> Result<Client, Error> {
    let addr = &config.openrgb.address;
    Client::connect(addr.as_str(), &config.openrgb.client_name).map_err(|e| {
        format!("cannot reach the OpenRGB server at {addr}: {e} (in OpenLinkHub: enableOpenRGBTargetServer)").into()
    })
}

/// A block of `width` cells in the colour (24-bit ANSI background), only on an interactive
/// terminal so piped output stays plain text.
fn swatch(c: Rgb, width: usize) -> String {
    if !std::io::stdout().is_terminal() {
        return String::new();
    }
    format!(
        "\x1b[48;2;{};{};{}m{}\x1b[0m ",
        c.r,
        c.g,
        c.b,
        " ".repeat(width)
    )
}
