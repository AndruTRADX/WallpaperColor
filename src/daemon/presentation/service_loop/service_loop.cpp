#include "daemon/presentation/service_loop/service_loop.hpp"

#include <chrono>

#include "daemon/application/features/apply_settings/apply_settings.hpp"
#include "daemon/application/features/exchange_messages/exchange_messages.hpp"
#include "daemon/application/features/paint_wallpaper/paint_wallpaper.hpp"
#include "daemon/application/features/white_test/white_test.hpp"
#include "daemon/presentation/requests/requests.hpp"

namespace wpc {

using namespace std::chrono_literals;

namespace {
/// Quiet time that ends a burst of notifications.
constexpr auto SETTLE = 800ms;
}  // namespace

ServiceLoop::ServiceLoop(ServiceParts parts, Notifications& notifications, DesktopChangedEvent& desktop_changed)
    : parts_(parts), notifications_(notifications), desktop_changed_(desktop_changed) {
    apply_settings(state_, parts_.settings, parts_.drivers, parts_.memory, true);
}

void ServiceLoop::run() {
    check_wallpaper(false);
    while (true) {
        std::optional<Notification> notification;
        if (std::optional<TimePoint::duration> wait = time_to_retry(state_, parts_.clock)) {
            notification = notifications_.receive_for(*wait);
        } else {
            notification = notifications_.receive();
        }

        Todo todo;
        todo.check = !notification.has_value();  // the retry is due
        if (notification) take(std::move(*notification), todo);
        if (!todo.check && !todo.reload && !todo.force) continue;

        // the rest of the burst (requests are still answered at once)
        if (!todo.force) {
            while (std::optional<Notification> more = notifications_.receive_for(SETTLE)) take(std::move(*more), todo);
        }
        if (todo.reload) settings_changed();
        check_wallpaper(todo.reload || todo.force);
    }
}

void ServiceLoop::settings_changed() {
    apply_settings(state_, parts_.settings, parts_.drivers, parts_.memory, false);
}

void ServiceLoop::check_wallpaper(bool force) {
    if (state_.testing) return paint_white(state_, parts_.drivers, parts_.clock);
    ServiceParts& p = parts_;
    paint_wallpaper(state_, p.desktop, p.packages, p.scheme, p.pictures, p.drivers, p.clock, p.memory, force);
    desktop_changed_.follow(state_.slide_folders, state_.shown.image);
}

void ServiceLoop::take(Notification notification, Todo& todo) {
    if (std::holds_alternative<DesktopChangedNotice>(notification)) {
        todo.check = true;
    } else if (std::holds_alternative<SettingsChangedNotice>(notification)) {
        todo.reload = true;
    } else {
        RequestNotice& r = std::get<RequestNotice>(notification);
        ReadRequest read = read_request(parts_.codec, r.line);
        ipc::Response answer = read.request ? run_request(*read.request, state_, todo.force)
                                            : ipc::Response::error(read.problem);
        r.reply->send(write_answer(parts_.codec, answer));
    }
}

}  // namespace wpc
