// Terminal UI: what the service is doing, a preview of every device, and the settings.
//
// Settings are saved to the config file as they change (the service reloads it within about
// a second); the preview is computed here at once from the same picture, so changes show
// immediately. Closing the UI leaves the service running.

#pragma once

#include <filesystem>

namespace wpc {

/// Runs the terminal UI until the user quits. Throws wpc::Error.
void run_tui(const std::filesystem::path& config_path);

}  // namespace wpc
