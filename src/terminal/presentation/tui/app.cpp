#include "terminal/presentation/tui/app.hpp"

#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>
#include <stop_token>
#include <thread>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "terminal/application/features/control_service/control_service.hpp"
#include "terminal/application/features/extract_colours/extract_colours.hpp"
#include "terminal/application/features/preview_devices/preview_devices.hpp"
#include "terminal/application/features/save_settings/save_settings.hpp"
#include "terminal/application/features/service_status/service_status.hpp"
#include "terminal/presentation/tui/tui.hpp"

namespace wpc::tui {

void App::run() {
    ui::ScreenInteractive screen = ui::ScreenInteractive::Fullscreen();
    screen.TrackMouse(false);  // the UI has no mouse: motion would only ask the service again

    // Every second, ask the event loop to refresh (this thread only exists while the UI is open).
    std::mutex mutex;
    std::condition_variable_any tick;
    std::jthread ticker([&](std::stop_token stop) {
        std::unique_lock lock(mutex);
        while (!tick.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; })) {
            if (stop.stop_requested()) return;
            screen.PostEvent(ui::Event::Custom);
        }
    });

    refresh();
    ui::Component renderer = ui::Renderer([&] { return drivers_page_ ? draw_drivers() : draw_home(); });
    ui::Component component = ui::CatchEvent(renderer, [&](const ui::Event& event) {
        if (event == ui::Event::Custom) {
            refresh();
            return true;
        }
        on_key(event);
        refresh();  // show the effect of the key at once
        if (quit_) screen.Exit();
        return true;
    });
    screen.Loop(component);
}

void App::refresh() {
    try {
        ServiceStatus s = service_status(parts_.service, parts_.codec);
        status_ = std::move(s.status);
        status_stops_ = std::move(s.stops);
    } catch (const Error& e) {
        status_.reset();
        status_error_ = e.what();
    }
    if (status_ && status_->image && preview_picture_ != status_->image) recompute(*status_->image);
}

/// Local preview with the settings being edited.
void App::recompute(const std::filesystem::path& picture) {
    PictureColours result = extract_colours(parts_.pictures, {picture}, config_.extract).at(0);
    preview_ = result.colours;
    preview_picture_ = picture;
}

void App::on_key(const ui::Event& e) {
    if (drivers_page_) return on_drivers_key(e);
    if (e == ui::Event::Character('d')) return open_drivers_page();
    on_home_key(e);
}

void App::saved(bool edited, const std::string& done) {
    if (!edited) {
        message_ = "this setting has a layout the UI does not edit: change it in the config file";
        return;
    }
    SavedSettings result = save_settings(editor_, parts_.format, config_);
    config_ = result.config;
    message_ = result.error ? std::format("cannot save the config: {}", *result.error)
                           : done + "; the service applies it in about a second";
    if (preview_picture_ && result.extract_changed) recompute(std::filesystem::path(*preview_picture_));
}

void App::ask(ipc::Request request, const std::string& done) {
    try {
        control_service(parts_.service, parts_.codec, request);
        message_ = done;
    } catch (const Error& err) {
        message_ = err.what();
    }
}

std::vector<Rgb> App::stops() const {
    return preview_colours(preview_, config_.led.mode, status_ ? status_stops_ : std::vector<Rgb>{});
}

const std::vector<ipc::DeviceInfo>& App::devices() const {
    static const std::vector<ipc::DeviceInfo> none;
    return status_ ? status_->devices : none;
}

}  // namespace wpc::tui

namespace wpc {

void run_tui(TerminalParts& parts, SettingsEditor& editor) {
    tui::App app(parts, editor);
    app.run();
}

}  // namespace wpc
