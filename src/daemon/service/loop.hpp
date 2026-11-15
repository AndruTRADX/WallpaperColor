// The service's loop. Receives the events' notifications one at a time and runs the use cases
// they ask for: a burst of notifications (a slideshow folder rescan opens every file) runs them
// once, when it ends; requests are answered at once; a pending retry runs when it is due.
//
// It keeps these rules, so the service never goes deaf (tests/test_service.cpp checks each one
// over time with a fake clock):
//   R1  while it doesn't know what is on screen, a retry or a signal is armed to wake it;
//   R2  a due retry is always run and either cleared or moved on (never left due: it would spin);
//   R3  the folder watches match the folders on disk;
//   R4  hardware that comes back is painted again without waiting for the next wallpaper;
//   R5  what hears events doesn't stop silently.

#pragma once

#include <chrono>
#include <optional>

#include "daemon/service/events.hpp"
#include "daemon/service/service.hpp"
#include "daemon/service/state.hpp"

namespace wpc {

class ServiceLoop {
public:
    /// Quiet time that ends a burst of notifications. Plasma answers with the new slide as soon
    /// as it opens it (measured: under 10 ms), so this only merges bursts; every check after
    /// the first costs a D-Bus call and paints nothing when nothing changed.
    static constexpr std::chrono::milliseconds SETTLE{300};
    /// The longest a burst is merged: notifications that never stop don't keep the service
    /// from painting.
    static constexpr std::chrono::seconds LONGEST_BURST{3};

    /// Applies the settings. `settle`: how long a burst waits to be over (the tests use zero).
    ServiceLoop(Ports ports, Notifications& notifications, DesktopChangedEvent& desktop_changed,
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
        bool check = false;    // the wallpaper may have changed (or a retry is due)
        bool reload = false;   // the settings may have changed
        bool force = false;    // paint again now (a request)
        bool repaint = false;  // paint again, after the burst (the hardware changed)
        bool woke = false;     // what the devices show is not known
    };
    void take(Notification notification, Todo& todo);

    Ports ports_;
    Notifications& notifications_;
    DesktopChangedEvent& desktop_changed_;
    std::chrono::milliseconds settle_;
    ServiceState state_;
};

}  // namespace wpc
