// wallpapercolor: terminal client for WallpaperColor. Without a subcommand: the terminal UI.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

#include <CLI/CLI.hpp>

#include "core/config.hpp"
#include "core/extract.hpp"
#include "util/error.hpp"

namespace fs = std::filesystem;
using namespace wpc;

namespace {

/// Prints one line to stdout. Exits quietly when the reader went away
/// (`wallpapercolor ... | head`) instead of failing on a broken pipe.
void out(const std::string& line) {
    std::string text = line + "\n";
    if (std::fwrite(text.data(), 1, text.size(), stdout) != text.size() || std::fflush(stdout) != 0) {
        if (errno == EPIPE) std::exit(0);
        std::cerr << "cannot write to stdout: " << os_error(errno) << "\n";
        std::abort();
    }
}

/// A block of `width` cells in the colour (24-bit ANSI background), only on an interactive
/// terminal so piped output stays plain text.
std::string swatch(Rgb c, int width) {
    if (!isatty(STDOUT_FILENO)) return "";
    return std::format("\x1b[48;2;{};{};{}m{}\x1b[0m ", c.r, c.g, c.b, std::string(width, ' '));
}

int run_extract(const Config& config, const std::vector<fs::path>& images, bool palette) {
    bool failed = false;
    for (const fs::path& path : images) {
        try {
            Extraction e = extract_path(path, config.extract);
            std::string arc;
            for (Rgb c : e.arc) arc += swatch(c, 2);
            out(std::format("{}{}  arc {}{} {} {}  {}", swatch(e.led, 6), e.led.hex(), arc,
                            e.arc[0].hex(), e.arc[1].hex(), e.arc[2].hex(), path.string()));
            if (palette) {
                for (const PaletteEntry& p : e.palette) {
                    const char* mark = (e.picked == p.color) ? "←" : "";
                    out(std::format("    {}{} {:5.1f}% {}", swatch(p.color, 2), p.color.hex(),
                                    p.share * 100.0f, mark));
                }
            }
        } catch (const Error& err) {
            std::cerr << path.string() << ": " << err.what() << "\n";
            failed = true;
        }
    }
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

int run_config(const Config& config) {
    std::string text = config.to_toml();
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    out(text);
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);  // a closed pipe becomes EPIPE, handled in out()

    CLI::App app{"Paint your RGB hardware with the wallpaper colour", "wallpapercolor"};
    app.set_version_flag("-V,--version", std::string("wallpapercolor ") + WPC_VERSION);
    app.require_subcommand(0, 1);

    std::optional<fs::path> config_option;
    app.add_option("--config", config_option,
                   "Config file (default: ~/.config/wallpapercolor/config.toml)");

    CLI::App* extract_cmd = app.add_subcommand(
        "extract", "Show the LED colour that would be taken from one or more pictures");
    std::vector<fs::path> images;
    bool palette = false;
    extract_cmd->add_option("images", images, "Pictures to analyse")->required();
    extract_cmd->add_flag("-p,--palette", palette, "Also show the palette the colour was picked from");

    CLI::App* config_cmd =
        app.add_subcommand("config", "Print the effective configuration (defaults included) as TOML");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        int code = app.exit(e);
        return code == 0 ? 0 : 2;  // usage errors exit with 2, like clap
    }

    try {
        std::optional<fs::path> config_path = config_option ? config_option : Config::default_path();
        Config config = config_path ? Config::load(*config_path) : Config{};

        if (*extract_cmd) return run_extract(config, images, palette);
        if (*config_cmd) return run_config(config);
        std::cerr << "wallpapercolor: the terminal UI is not ported yet\n";
        return EXIT_FAILURE;
    } catch (const Error& e) {
        std::cerr << "wallpapercolor: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
