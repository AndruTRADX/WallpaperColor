#include "daemon/service/loop.hpp"

#include <algorithm>

namespace wpc {

ServiceLoop::ServiceLoop(Ports ports, Notifications& notifications, DesktopChangedEvent& desktop_changed,
                         std::chrono::milliseconds settle)
    : ports_(ports), notifications_(notifications), desktop_changed_(desktop_changed), settle_(settle) {
    apply_settings(state_, ports_.settings, ports_.drivers, true);
}

void ServiceLoop::run() {
    check_wallpaper(false);
    while (true) step();
}

bool ServiceLoop::step(std::optional<TimePoint::duration> limit) {
    std::optional<TimePoint::duration> wait = time_to_retry(state_, ports_.clock);
    if (limit) wait = wait ? std::min(*wait, *limit) : *limit;
    std::optional<Notification> notification = wait ? notifications_.receive_for(*wait) : notifications_.receive();

    Todo todo;
    if (notification) {
        take(std::move(*notification), todo);
    } else {
        std::optional<TimePoint::duration> left = time_to_retry(state_, ports_.clock);
        if (!left || *left > TimePoint::duration::zero()) return false;  // the limit, not the retry
        todo.check = true;
    }
    if (!todo.check && !todo.reload && !todo.force && !todo.repaint) return true;  // a request, answered

    // The rest of the burst (requests are still answered at once), for LONGEST_BURST at most.
    if (!todo.force) {
        auto give_up = std::chrono::steady_clock::now() + LONGEST_BURST;
        while (std::chrono::steady_clock::now() < give_up) {
            std::optional<Notification> more = notifications_.receive_for(settle_);
            if (!more) break;
            take(std::move(*more), todo);
        }
    }
    if (todo.reload) settings_changed();
    if (todo.woke) hardware_woke(state_);
    check_wallpaper(todo.reload || todo.force || todo.repaint);
    return true;
}

void ServiceLoop::settings_changed() {
    apply_settings(state_, ports_.settings, ports_.drivers, false);
}

void ServiceLoop::check_wallpaper(bool force) {
    if (state_.testing) return paint_white(state_, ports_.drivers, ports_.clock);
    paint_wallpaper(state_, ports_, force, [this](const std::filesystem::path& picture) { desktop_changed_.reading(picture); });
    // A folder that cannot be watched now (removed, for a while): try again later (R1, R3).
    if (std::optional<std::string> problem = desktop_changed_.follow(state_.slide_folders)) {
        schedule_retry(state_, ports_.clock, *problem);
    }
}

void ServiceLoop::take(Notification notification, Todo& todo) {
    if (std::holds_alternative<DesktopChangedNotice>(notification)) {
        todo.check = true;
    } else if (std::holds_alternative<SettingsChangedNotice>(notification)) {
        todo.reload = true;
    } else if (const auto* hardware = std::get_if<HardwareChangedNotice>(&notification)) {
        todo.repaint = true;
        todo.woke = todo.woke || hardware->woke;
    } else {
        RequestNotice& r = std::get<RequestNotice>(notification);
        if (r.reply->given_up()) return;  // answered "busy" already: it must not happen now
        r.reply->send(answer_line(r.line, state_, todo.force));
    }
}

}  // namespace wpc
