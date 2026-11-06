// Presentation: the service's loop. Receives the events' notifications one at a time and runs
// the features they ask for: a burst of notifications (a slideshow folder rescan opens every
// file) runs them once, when it ends; requests are answered at once; a pending retry runs when
// it is due.

#pragma once

#include <chrono>
#include <optional>

#include "daemon/application/events/desktop_changed.hpp"
#include "daemon/application/models/notifications.hpp"
#include "daemon/application/models/service_parts.hpp"
#include "daemon/application/models/service_state.hpp"

namespace wpc {

class ServiceLoop {
public:
    /// Quiet time that ends a burst of notifications.
    static constexpr std::chrono::milliseconds SETTLE{800};

    /// Applies the settings. `settle`: how long a burst waits to be over (the tests use zero).
    ServiceLoop(ServiceParts parts, Notifications& notifications, DesktopChangedEvent& desktop_changed,
                std::chrono::milliseconds settle = SETTLE);

    /// Paints the wallpaper, then runs until the service stops.
    [[noreturn]] void run();

    /// One turn of the loop: waits for a notification, or for the pending retry to be due, and
    /// runs what it asks for. `limit` caps the wait (the tests pass zero, so it never blocks).
    /// False when nothing came and no retry was due in that time.
    bool step(std::optional<TimePoint::duration> limit = std::nullopt);

    /// What the loop does with one kind of notification (also for the tests).
    void settings_changed();
    void check_wallpaper(bool force);
    const ServiceState& state() const { return state_; }

private:
    struct Todo {
        bool check = false;   // the wallpaper may have changed (or a retry is due)
        bool reload = false;  // the settings may have changed
        bool force = false;   // paint again now (a request)
        bool repaint = false; // paint again, after the burst (the hardware changed)
        bool woke = false;    // what the devices show is not known
    };
    void take(Notification notification, Todo& todo);

    ServiceParts parts_;
    Notifications& notifications_;
    DesktopChangedEvent& desktop_changed_;
    std::chrono::milliseconds settle_;
    ServiceState state_;
};

}  // namespace wpc
