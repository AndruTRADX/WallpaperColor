// Presentation: the service's loop. Receives the events' notifications one at a time and runs
// the features they ask for: a burst of notifications (a slideshow folder rescan opens every
// file) runs them once, when it ends; requests are answered at once; a pending retry runs when
// it is due.

#pragma once

#include "daemon/application/events/desktop_changed.hpp"
#include "daemon/application/models/notifications.hpp"
#include "daemon/application/models/service_parts.hpp"
#include "daemon/application/models/service_state.hpp"

namespace wpc {

class ServiceLoop {
public:
    /// Applies the settings.
    ServiceLoop(ServiceParts parts, Notifications& notifications, DesktopChangedEvent& desktop_changed);

    /// Paints the wallpaper, then runs until the service stops.
    [[noreturn]] void run();

    /// What the loop does with one kind of notification (also for the tests).
    void settings_changed();
    void check_wallpaper(bool force);
    const ServiceState& state() const { return state_; }

private:
    struct Todo {
        bool check = false;   // the wallpaper may have changed (or a retry is due)
        bool reload = false;  // the settings may have changed
        bool force = false;   // paint again now (a request)
    };
    void take(Notification notification, Todo& todo);

    ServiceParts parts_;
    Notifications& notifications_;
    DesktopChangedEvent& desktop_changed_;
    ServiceState state_;
};

}  // namespace wpc
