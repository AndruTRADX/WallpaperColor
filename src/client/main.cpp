// wallpapercolor: terminal client for WallpaperColor. Without a subcommand: the terminal UI.

#include <cctype>
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

#include "client/tui.hpp"
#include "core/config.hpp"
#include "core/extract.hpp"
#include "core/ipc.hpp"
#include "openrgb/client.hpp"
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

/// `items` joined with `separator`.
std::string join(const std::vector<std::string>& items, std::string_view separator) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += separator;
        out += items[i];
    }
    return out;
}

/// Asks the service; a failure means it is not running.
ipc::Response call_service(ipc::Request request) {
    try {
        return ipc::call(request);
    } catch (const Error& e) {
        throw Error(std::format(
            "the service is not running ({}): systemctl --user start wallpapercolord", e.what()));
    }
}

openrgb::Client connect(const Config& config) {
    const std::string& address = config.openrgb.address;
    try {
        return openrgb::Client::connect(address, config.openrgb.client_name);
    } catch (const Error& e) {
        throw Error(std::format(
            "cannot reach the OpenRGB server at {}: {} (in OpenLinkHub: enableOpenRGBTargetServer)",
            address, e.what()));
    }
}

// ---------------------------------------------------------------------------------------------
// Commands

int run_status() {
    ipc::Response response = call_service(ipc::Request::Status);
    if (response.kind == ipc::Response::Kind::Error) throw Error(response.message);
    if (response.kind != ipc::Response::Kind::Status) throw Error("the service gave no status");
    const ipc::Status& s = response.status;
    out(std::format("{} v{}  mode {}", s.paused ? "paused" : "running", s.version, mode_name(s.mode)));
    out(std::format("wallpaper  {} ({})", s.image ? s.image->string() : "—", s.plugin));
    std::vector<std::string> stops;
    for (const std::string& hex : s.stops) {
        std::optional<Rgb> c = Rgb::parse(hex);
        stops.push_back(c ? swatch(*c, 2) + hex : hex);
    }
    out("colours    " + join(stops, " "));
    for (const ipc::DeviceInfo& d : s.devices) {
        std::vector<std::string> zones;
        for (const ipc::ZoneInfo& z : d.zones) zones.push_back(std::format("{} ({})", z.name, z.leds));
        out(std::format("device     {}: {}", d.name, join(zones, ", ")));
    }
    if (s.error) out("problem    " + *s.error);
    return EXIT_SUCCESS;
}

int run_simple_request(ipc::Request request, std::string_view done) {
    ipc::Response response = call_service(request);
    if (response.kind == ipc::Response::Kind::Error) throw Error(response.message);
    out(std::string(done));
    return EXIT_SUCCESS;
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

int run_devices(const Config& config) {
    openrgb::Client client = connect(config);
    std::vector<openrgb::Controller> controllers = client.controllers();
    out(std::format("{} (protocol {}): {} device(s)", config.openrgb.address, client.protocol(),
                    controllers.size()));
    if (controllers.empty()) {
        out("  none exposed: in OpenLinkHub, turn on \"OpenRGB Integration\" for a device");
    }
    for (const openrgb::Controller& c : controllers) {
        Rgb now = c.colors.empty() ? Rgb{} : c.colors[0];
        out(std::format("  [{}] {}{}  {} LEDs  now {}", c.index, swatch(now, 2), c.name, c.led_count(), now.hex()));
        for (const openrgb::Zone& z : c.zones) out(std::format("        {}: {} LEDs", z.name, z.leds));
    }
    return EXIT_SUCCESS;
}

struct SetArgs {
    std::vector<std::string> colors;
    std::optional<fs::path> image;
    bool arc = false;
    bool single = false;
};

int run_set(const Config& config, const SetArgs& args) {
    Mode mode = args.arc ? Mode::Arc : args.single ? Mode::Single : config.led.mode;

    std::vector<Rgb> stops;
    if (args.image) {
        stops = extract_path(*args.image, config.extract).stops(mode);
    } else {
        for (const std::string& text : args.colors) stops.push_back(*Rgb::parse(text));
        if (mode == Mode::Single) stops.resize(1);
    }

    openrgb::Client client = connect(config);
    std::vector<openrgb::Controller> controllers = client.controllers();
    if (controllers.empty()) {
        throw Error("no devices exposed: turn on \"OpenRGB Integration\" in OpenLinkHub");
    }
    std::vector<std::string> label;
    for (Rgb c : stops) label.push_back(swatch(c, 2) + c.hex());
    for (const openrgb::Controller& c : controllers) {
        std::vector<Rgb> leds = c.paint(stops, [&](std::string_view zone) {
            return config.gain(c.name, zone);
        });
        client.set_leds(c.index, leds);
        out(std::format("{} → [{}] {}", join(label, " "), c.index, c.name));
    }
    return EXIT_SUCCESS;
}

int run_config(const Config& config) {
    std::string text = config.to_toml();
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    out(text);
    return EXIT_SUCCESS;
}

int open_tui(const std::optional<fs::path>& config_path) {
    if (!config_path) throw Error("no config path: set HOME or XDG_CONFIG_HOME");
    run_tui(*config_path);
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);  // a closed pipe becomes EPIPE, handled in out()

    CLI::App app{"Paint your RGB hardware with the wallpaper colour", "wallpapercolor"};
    app.set_version_flag("-V,--version", std::string("wallpapercolor ") + WPC_VERSION);
    app.require_subcommand(0, 1);
    app.fallthrough();  // --config also works after the subcommand

    std::optional<fs::path> config_option;
    app.add_option("--config", config_option,
                   "Config file (default: ~/.config/wallpapercolor/config.toml)");

    CLI::App* extract_cmd = app.add_subcommand(
        "extract", "Show the LED colour that would be taken from one or more pictures");
    std::vector<fs::path> images;
    bool palette = false;
    extract_cmd->add_option("images", images, "Pictures to analyse")->required();
    extract_cmd->add_flag("-p,--palette", palette, "Also show the palette the colour was picked from");

    CLI::App* devices_cmd = app.add_subcommand(
        "devices", "List the RGB devices the server exposes (in OpenLinkHub: \"OpenRGB Integration\" on)");

    CLI::App* set_cmd = app.add_subcommand("set", "Paint every exposed device with a colour, or three as an arc");
    SetArgs set_args;
    CLI::Validator colour_check(
        [](std::string& text) { return Rgb::parse(text) ? std::string() : invalid_colour_message(text); },
        "COLOR");
    CLI::Option* colors_opt = set_cmd->add_option("colors", set_args.colors,
                                                  "Colour(s) as #rrggbb or rrggbb; up to three with --arc")
                                  ->expected(1, 3)
                                  ->check(colour_check);
    CLI::Option* image_opt =
        set_cmd->add_option("--image", set_args.image, "Take the colour(s) from this picture instead");
    colors_opt->excludes(image_opt);
    CLI::Option* arc_opt = set_cmd->add_flag(
        "--arc", set_args.arc,
        "Spread three colours as a gradient: around each ring (fans, pump), top to bottom on RAM "
        "(default when the config says mode = \"arc\")");
    CLI::Option* single_opt = set_cmd->add_flag(
        "--single", set_args.single, "One colour on every LED (default when the config says mode = \"single\")");
    arc_opt->excludes(single_opt);

    CLI::App* config_cmd =
        app.add_subcommand("config", "Print the effective configuration (defaults included) as TOML");
    CLI::App* status_cmd = app.add_subcommand("status", "Show what the service is doing");
    CLI::App* pause_cmd =
        app.add_subcommand("pause", "Stop sending colours until resume (the service keeps running)");
    CLI::App* resume_cmd = app.add_subcommand("resume", "Start sending colours again");
    CLI::App* apply_cmd = app.add_subcommand("apply", "Send the current wallpaper's colours again now");
    CLI::App* tui_cmd = app.add_subcommand("tui", "Open the terminal UI (the default)");

    try {
        app.parse(argc, argv);
        if (*set_cmd && set_args.colors.empty() && !set_args.image) {
            throw CLI::RequiredError("COLORS (or --image)");
        }
    } catch (const CLI::ParseError& e) {
        int code = app.exit(e);
        return code == 0 ? 0 : 2;  // usage errors exit with 2
    }

    try {
        std::optional<fs::path> config_path = config_option ? config_option : Config::default_path();
        Config config = config_path ? Config::load(*config_path) : Config{};

        if (*extract_cmd) return run_extract(config, images, palette);
        if (*devices_cmd) return run_devices(config);
        if (*set_cmd) return run_set(config, set_args);
        if (*config_cmd) return run_config(config);
        if (*status_cmd) return run_status();
        if (*pause_cmd) return run_simple_request(ipc::Request::Pause, "paused");
        if (*resume_cmd) return run_simple_request(ipc::Request::Resume, "resumed");
        if (*apply_cmd) return run_simple_request(ipc::Request::Apply, "colours sent again");
        (void)tui_cmd;  // `tui` and no subcommand both open the terminal UI
        return open_tui(config_path);
    } catch (const Error& e) {
        std::cerr << "wallpapercolor: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
