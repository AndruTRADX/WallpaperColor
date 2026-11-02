//! `wallpapercolor`: terminal client for WallpaperColor.

use clap::{Args, Parser, Subcommand};
use std::io::IsTerminal;
use std::path::PathBuf;
use std::process::ExitCode;
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
    command: Command,
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
    /// Paint every exposed device with one colour
    Set(SetArgs),
    /// Print the effective configuration (defaults included) as TOML
    Config,
}

#[derive(Args)]
#[group(required = true, multiple = false)]
struct SetArgs {
    /// Colour as #rrggbb or rrggbb
    color: Option<Rgb>,
    /// Take the colour from this picture instead
    #[arg(long)]
    image: Option<PathBuf>,
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
    let config = match cli.config.clone().or_else(Config::default_path) {
        Some(path) => Config::load(&path)?,
        None => Config::default(),
    };

    match cli.command {
        Command::Extract { images, palette } => {
            let mut failed = false;
            for path in &images {
                match extract_path(path, &config.extract) {
                    Ok(e) => {
                        out!("{}{}  {}", swatch(e.led, 6), e.led, path.display());
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
            let color = match (args.color, args.image) {
                (Some(c), _) => c,
                (None, Some(path)) => extract_path(&path, &config.extract)?.led,
                (None, None) => unreachable!("clap requires one of them"),
            };
            let mut client = connect(&config)?;
            let ctrls = client.controllers()?;
            if ctrls.is_empty() {
                return Err(
                    "no devices exposed: turn on \"OpenRGB Integration\" in OpenLinkHub".into(),
                );
            }
            for c in &ctrls {
                client.set_all(c, color)?;
                out!("{}{color} → [{}] {}", swatch(color, 2), c.index, c.name);
            }
            Ok(ExitCode::SUCCESS)
        }
        Command::Config => {
            out!("{}", config.to_toml().trim_end());
            Ok(ExitCode::SUCCESS)
        }
    }
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
