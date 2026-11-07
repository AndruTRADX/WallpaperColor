// One thread waiting for many things at once (Linux epoll): file descriptors becoming readable,
// a time passing, or another thread waking it. The thread sleeps in the kernel until one of
// them happens, then runs what that source asked for. Every callback runs on the thread that
// calls run_once(), so what the callbacks share needs no lock.

#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <optional>

#include <sys/epoll.h>
#include <unistd.h>

#include "utils/error.hpp"
#include "utils/socket.hpp"

namespace wpc {

class EventLoop {
public:
    using Duration = std::chrono::steady_clock::duration;
    /// Asked before every sleep: the longest the loop may sleep for that source (nullopt: no
    /// limit). A source that keeps its own queue (D-Bus) empties it here and asks for zero.
    using Prepare = std::function<std::optional<Duration>()>;

    /// Throws wpc::Error.
    EventLoop();
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /// Calls `ready` (on the loop's thread) while `fd` has any of `events` (EPOLLIN, EPOLLOUT).
    /// The fd stays its owner's: remove() it before closing it. Throws wpc::Error.
    void add(int fd, uint32_t events, std::function<void()> ready, Prepare prepare = {});
    /// Waits for other `events` on `fd` from now on. Throws wpc::Error.
    void change(int fd, uint32_t events);
    /// Stops waiting for `fd`; its callbacks are not called again, even in this turn.
    void remove(int fd);

    /// Sleeps until a source is ready, `timeout` passes (nullopt: as long as it takes) or
    /// wake() is called, and runs the ready sources. False when nothing happened in time.
    /// A callback's exception comes out of here. Throws wpc::Error.
    bool run_once(std::optional<Duration> timeout);

    /// Ends the current or the next sleep of run_once(). Any thread may call it.
    void wake();

private:
    struct Source {
        int fd;
        std::function<void()> ready;
        Prepare prepare;
    };
    std::optional<Duration> longest_sleep(std::optional<Duration> timeout);

    Fd epoll_;
    Fd wake_;  // an eventfd: wake() writes to it
    std::map<uint64_t, Source> sources_;  // by an id never used again (an fd number is reused)
    uint64_t next_id_ = 1;                // 0 is wake_
};

/// The fds a D-Bus connection is waited for with, to stop waiting for them (remove_bus).
struct BusFds {
    int fd = -1;
    int event_fd = -1;
};

/// Runs a D-Bus connection (sdbus-c++'s IConnection) on `loop`: its incoming messages are read
/// and its callbacks run on the loop's thread, with no thread of its own. A connection that
/// breaks throws wpc::Error out of run_once(). Call remove_bus() before the connection goes.
/// Throws wpc::Error.
template <typename Connection>
BusFds add_bus(EventLoop& loop, Connection& bus) {
    // The connection's failures, as the service's errors.
    auto lost = [](const std::exception& e) { return Error(cat("D-Bus connection lost: ", e.what())); };
    // Every queued message, read or written: true if there was any.
    auto process = [&bus, lost] {
        try {
            bool any = false;
            while (bus.processPendingEvent()) any = true;
            return any;
        } catch (const std::exception& e) {
            throw lost(e);
        }
    };
    // A blocking call reads the signals that arrive meanwhile into the connection's queue, where
    // the fd no longer shows them: they are run here, before sleeping. Then the fd is waited
    // for what the connection needs (reading, writing) for at most its own timeout.
    auto prepare = [&loop, &bus, process, lost]() -> std::optional<EventLoop::Duration> {
        if (process()) return EventLoop::Duration::zero();
        try {
            auto now = bus.getEventLoopPollData();
            loop.change(now.fd, static_cast<uint16_t>(now.events));
            int ms = now.getPollTimeout();
            if (ms < 0) return std::nullopt;
            return std::chrono::milliseconds(ms);
        } catch (const Error&) {
            throw;
        } catch (const std::exception& e) {
            throw lost(e);
        }
    };

    BusFds fds;
    try {
        auto poll = bus.getEventLoopPollData();
        fds = {poll.fd, poll.eventFd};  // eventFd: written by sdbus-c++ when messages wait in its queue
        loop.add(fds.fd, static_cast<uint16_t>(poll.events), [process] { process(); }, prepare);
    } catch (const Error&) {
        throw;
    } catch (const std::exception& e) {
        throw lost(e);
    }
    try {
        loop.add(fds.event_fd, EPOLLIN, [process, event_fd = fds.event_fd] {
            uint64_t count;
            [[maybe_unused]] ssize_t n = ::read(event_fd, &count, sizeof count);
            process();
        });
    } catch (...) {
        loop.remove(fds.fd);  // all or nothing: the caller may destroy the connection now
        throw;
    }
    return fds;
}

/// Stops running a connection on `loop`, even one that is lost. Never throws.
inline void remove_bus(EventLoop& loop, BusFds fds) {
    loop.remove(fds.fd);
    loop.remove(fds.event_fd);
}

}  // namespace wpc
