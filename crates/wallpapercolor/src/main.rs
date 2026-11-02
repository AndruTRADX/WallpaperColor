//! `wallpapercolor`: terminal client for WallpaperColor.

use clap::{Parser, Subcommand};
use std::io::IsTerminal;
use std::path::PathBuf;
use std::process::ExitCode;
use wpc_core::{Config, Rgb, extract_path};

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
    /// Print the effective configuration (defaults included) as TOML
    Config,
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    let config = match cli.config.clone().or_else(Config::default_path) {
        Some(path) => match Config::load(&path) {
            Ok(c) => c,
            Err(e) => {
                eprintln!("wallpapercolor: {e}");
                return ExitCode::FAILURE;
            }
        },
        None => Config::default(),
    };

    match cli.command {
        Command::Extract { images, palette } => {
            let mut failed = false;
            for path in &images {
                match extract_path(path, &config.extract) {
                    Ok(e) => {
                        println!("{}{}  {}", swatch(e.led, 6), e.led, path.display());
                        if palette {
                            for (c, share) in &e.palette {
                                let mark = if Some(*c) == e.picked { "←" } else { "" };
                                println!("    {}{c} {:5.1}% {mark}", swatch(*c, 2), share * 100.0);
                            }
                        }
                    }
                    Err(err) => {
                        eprintln!("{}: {err}", path.display());
                        failed = true;
                    }
                }
            }
            if failed {
                ExitCode::FAILURE
            } else {
                ExitCode::SUCCESS
            }
        }
        Command::Config => {
            print!("{}", config.to_toml());
            ExitCode::SUCCESS
        }
    }
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
