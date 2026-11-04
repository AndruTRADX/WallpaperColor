// The terminal UI's state and its two pages. Its methods are spread by page:
//   app.cpp           the loop, refreshing, keys every page shares
//   home_page.cpp     the service, the devices' preview, the settings
//   drivers_page.cpp  which components are painted, and the drivers' settings
// What a key changes is done by a feature (terminal/application/features); this only draws and
// turns keys into feature calls.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "shared/domain/colours/extract.hpp"
#include "shared/domain/settings/settings.hpp"
#include "shared/models/protocol/messages.hpp"
#include "terminal/application/features/detect_hardware/detect_hardware.hpp"
#include "terminal/application/features/driver_options/driver_options.hpp"
#include "terminal/application/features/load_settings/load_settings.hpp"
#include "terminal/application/models/terminal_parts.hpp"

namespace wpc::tui {

namespace ui = ftxui;

class App {
public:
    App(TerminalParts& parts, SettingsEditor& editor)
        : parts_(parts), editor_(editor), config_(edited_settings(parts.format, editor)) {}

    void run();

private:
    /// One line of the settings list.
    struct Item {
        enum class Kind { Mode, Number, Gain, AddCalibration };
        Kind kind;
        size_t index = 0;  // number setting (Number) or calibration entry (Gain)
        int channel = 0;   // 0 red, 1 green, 2 blue (Gain)
    };

    // app.cpp
    void refresh();
    void recompute(const std::filesystem::path& picture);
    void on_key(const ui::Event& event);
    /// After an edit: save, take the new settings and update the preview.
    void saved(bool edited, const std::string& done);
    void ask(ipc::Request request, const std::string& done);
    std::vector<Rgb> stops() const;
    const std::vector<ipc::DeviceInfo>& devices() const;

    // home_page.cpp
    void on_home_key(const ui::Event& event);
    std::vector<Item> items() const;
    void adjust(const Item& item, int direction);
    void add_calibration();
    void remove_calibration(size_t entry);
    ui::Element draw_home();
    ui::Element draw_header() const;
    ui::Element draw_devices() const;
    ui::Element draw_settings();
    ui::Element draw_item(const Item& item, bool selected);

    // drivers_page.cpp
    void open_drivers_page();
    /// The drivers' settings shown under the components (from the last detection).
    std::vector<DriverOptionValue> options() const;
    void on_drivers_key(const ui::Event& event);
    ui::Element draw_drivers() const;

    TerminalParts& parts_;
    SettingsEditor& editor_;
    Config config_;
    /// The service's status, or why it could not be read.
    std::optional<ipc::Status> status_;
    /// Its colours.
    std::vector<Rgb> status_stops_;
    std::string status_error_;
    /// Picture the local preview was computed from, and its analysis.
    std::optional<std::filesystem::path> preview_picture_;
    std::optional<Extraction> preview_;
    std::string message_;
    bool quit_ = false;

    // The home page.
    size_t selected_ = 0;
    /// Which device or zone "+ Calibrate" points at.
    size_t add_choice_ = 0;
    /// Calibration entry waiting for a second `x` to be removed.
    std::optional<size_t> pending_remove_;

    // The Drivers page: open or not, what it detected and the selected line.
    bool drivers_page_ = false;
    std::optional<Detection> detection_;
    size_t driver_line_ = 0;
};

}  // namespace wpc::tui
