#include "daemon/presentation/service_loop/service_loop.hpp"

#include <algorithm>

#include "daemon/application/features/apply_settings/apply_settings.hpp"
#include "daemon/application/features/exchange_messages/exchange_messages.hpp"
#include "daemon/application/features/paint_wallpaper/paint_wallpaper.hpp"
#include "daemon/application/features/white_test/white_test.hpp"
#include "daemon/presentation/requests/requests.hpp"

namespace wpc {

ServiceLoop::ServiceLoop(ServiceParts parts, Notifications& notifications, DesktopChangedEvent& desktop_changed,
                         std::chrono::milliseconds settle)
    : parts_(parts), notifications_(notifications), desktop_changed_(desktop_changed), settle_(settle) {
    apply_settings(state_, parts_.settings, parts_.drivers, parts_.memory, true);
}

void ServiceLoop::run() {
    check_wallpaper(false);
    while (true) step();
}

bool ServiceLoop::step(std::optional<TimePoint::duration> limit) {
    std::optional<TimePoint::duration> wait = time_to_retry(state_, parts_.clock);
    if (limit) wait = wait ? std::min(*wait, *limit) : *limit;
    std::optional<Notification> notification = wait ? notifications_.receive_for(*wait) : notifications_.receive();

    Todo todo;
    if (notification) {
        take(std::move(*notification), todo);
    } else {
        std::optional<TimePoint::duration> left = time_to_retry(state_, parts_.clock);
        if (!left || *left > TimePoint::duration::zero()) return false;  // the limit, not the retry
        todo.check = true;
    }
    if (!todo.check && !todo.reload && !todo.force && !todo.repaint) return true;  // a request, answered

    // the rest of the burst (requests are still answered at once)
    if (!todo.force) {
        while (std::optional<Notification> more = notifications_.receive_for(settle_)) take(std::move(*more), todo);
    }
    if (todo.reload) settings_changed();
    if (todo.woke) hardware_woke(state_);
    check_wallpaper(todo.reload || todo.force || todo.repaint);
    return true;
}

void ServiceLoop::settings_changed() {
    apply_settings(state_, parts_.settings, parts_.drivers, parts_.memory, false);
}

void ServiceLoop::check_wallpaper(bool force) {
    if (state_.testing) return paint_white(state_, parts_.drivers, parts_.clock);
    ServiceParts& p = parts_;
    paint_wallpaper(state_, p.desktop, p.packages, p.scheme, p.pictures, p.drivers, p.clock, p.memory, force,
                    [this](const std::filesystem::path& picture) { desktop_changed_.showing(picture); });
    // A folder that cannot be watched now (removed, for a while): try again later (R1, R3).
    if (std::optional<std::string> problem = desktop_changed_.follow(state_.slide_folders)) {
        schedule_retry(state_, p.clock, *problem);
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
        ReadRequest read = read_request(parts_.codec, r.line);
        ipc::Response answer = read.request ? run_request(*read.request, state_, todo.force)
                                            : ipc::Response::error(read.problem);
        r.reply->send(write_answer(parts_.codec, answer));
    }
}

}  // namespace wpc
