// The terminal's commands. Each one reads its options (parsed in terminal/main.cpp), calls a
// feature (terminal/application/features) and prints the result. Each returns the exit code.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "terminal/application/features/choose_components/choose_components.hpp"
#include "terminal/application/features/set_colours/set_colours.hpp"
#include "terminal/application/models/terminal_parts.hpp"

namespace wpc {

int run_status(TerminalParts& parts);

/// `pause`, `resume`, `apply`: sends `request` and prints `done`.
int run_request(TerminalParts& parts, ipc::Request request, std::string_view done);

int run_extract(TerminalParts& parts, const Config& config, const std::vector<std::filesystem::path>& pictures,
                bool palette);

int run_devices(TerminalParts& parts, const Config& config);

int run_set(TerminalParts& parts, const Config& config, const ColoursToSet& what);

/// Without `action`: what is chosen and which driver paints it. With "on"/"off": chooses
/// `names`.
int run_drivers(TerminalParts& parts, const Config& config, SettingsEditor& editor, const std::string& action,
                const std::vector<std::string>& names);

/// The settings in use as TOML, defaults included.
int run_config(TerminalParts& parts, const Config& config);

}  // namespace wpc
