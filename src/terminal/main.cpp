// wallpapercolor: the terminal program. Without a subcommand: the terminal UI.
//
// This file reads the command line, picks the implementations of the contracts (the "composition root") and hands them
// to the command. Commands: presentation/cli; the terminal UI: presentation/tui; what they do:
// application/features.

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

#include "shared/infrastructure/services/socket_path.hpp"
#include "terminal/application/features/load_settings/load_settings.hpp"
#include "terminal/infrastructure/external/driver_modules.hpp"
#include "terminal/infrastructure/external/message_codec.hpp"
#include "terminal/infrastructure/external/pictures.hpp"
#include "terminal/infrastructure/external/settings_file.hpp"
#include "terminal/infrastructure/services/service_socket.hpp"
#include "terminal/infrastructure/services/settings_editor.hpp"
#include "terminal/presentation/cli/commands.hpp"
#include "terminal/presentation/tui/tui.hpp"
#include "utils/error.hpp"

namespace fs = std::filesystem;
using namespace wpc;

namespace {

/// `set`'s options as the command line gives them.
struct SetArgs {
    std::vector<std::string> colors;
    std::optional<fs::path> image;
    bool arc = false;
    bool single = false;
    bool split = false;
};

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
        "devices", "List what every installed driver finds, zone by zone, and whether it is painted");

    CLI::App* drivers_cmd = app.add_subcommand(
        "drivers", "Show which components are painted and by which driver; `on`/`off` chooses them");
    std::string drivers_action;
    std::vector<std::string> drivers_names;
    drivers_cmd->add_option("action", drivers_action, "on or off")->check(CLI::IsMember({"on", "off"}));
    drivers_cmd->add_option("components", drivers_names, "fans, pump, ram, motherboard, argb, keyboard, gpu, strip, other");

    CLI::App* set_cmd = app.add_subcommand("set", "Paint every chosen component with a colour, or three as an arc");
    SetArgs set_args;  // read below into ColoursToSet
    CLI::Validator colour_check(
        [](std::string& text) { return Rgb::parse(text) ? std::string() : invalid_colour_message(text); },
        "COLOR");
    CLI::Option* colors_opt = set_cmd->add_option("colors", set_args.colors,
                                                  "Colour(s) as #rrggbb or rrggbb; up to three with --arc or --split")
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
    CLI::Option* split_opt = set_cmd->add_flag(
        "--split", set_args.split,
        "One colour per kind of zone: RAM, pump, fans (default when the config says mode = \"split\")");
    arc_opt->excludes(single_opt)->excludes(split_opt);
    single_opt->excludes(split_opt);

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
        // The implementations the commands work with.
        UnixServiceSocket service(SocketPath().path());
        ProtocolJsonCodec codec;
        HostDriverModules drivers;
        PictureFilesAdapter pictures;
        TomlSettingsFormat format;
        TerminalParts parts{service, codec, drivers, pictures, format};

        LoadedSettings settings = load_settings(format, config_option);
        const Config& config = settings.config;
        auto editor = [&] {
            if (!settings.path) throw Error("no config path: set HOME or XDG_CONFIG_HOME");
            return TomlSettingsEditor::open(*settings.path);
        };

        if (*extract_cmd) return run_extract(parts, config, images, palette);
        if (*devices_cmd) return run_devices(parts, config);
        if (*drivers_cmd) {
            TomlSettingsEditor edits = editor();
            return run_drivers(parts, config, edits, drivers_action, drivers_names);
        }
        if (*set_cmd) {
            std::optional<Mode> mode;
            if (set_args.arc) mode = Mode::Arc;
            if (set_args.single) mode = Mode::Single;
            if (set_args.split) mode = Mode::Split;
            return run_set(parts, config, ColoursToSet{set_args.colors, set_args.image, mode});
        }
        if (*config_cmd) return run_config(parts, config);
        if (*status_cmd) return run_status(parts);
        if (*pause_cmd) return run_request(parts, ipc::Request::Pause, "paused");
        if (*resume_cmd) return run_request(parts, ipc::Request::Resume, "resumed");
        if (*apply_cmd) return run_request(parts, ipc::Request::Apply, "colours sent again");
        (void)tui_cmd;  // `tui` and no subcommand both open the terminal UI
        TomlSettingsEditor edits = editor();
        run_tui(parts, edits);
        return EXIT_SUCCESS;
    } catch (const Error& e) {
        std::cerr << "wallpapercolor: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
