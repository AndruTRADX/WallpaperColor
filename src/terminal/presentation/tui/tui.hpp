// The terminal UI: what the service is doing, a preview of every device, the settings, and the
// Drivers page. Settings are saved as they change (the service reloads them within about a
// second); the preview is computed here at once from the same picture, so changes show
// immediately. Closing the UI leaves the service running.

#pragma once

#include "terminal/application/features/load_settings/load_settings.hpp"
#include "terminal/application/models/terminal_parts.hpp"

namespace wpc {

/// Runs the terminal UI until the user quits. Throws wpc::Error.
void run_tui(TerminalParts& parts, SettingsEditor& editor);

}  // namespace wpc
