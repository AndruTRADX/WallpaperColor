// The service's one wait (utils/event_loop, utils/event_queue): what the loop's sources, the
// queue and a D-Bus connection run on it rely on.

#include <doctest/doctest.h>

#include <chrono>
#include <stdexcept>
#include <thread>

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "utils/event_loop.hpp"
#include "utils/event_queue.hpp"

using namespace wpc;
using namespace std::chrono_literals;

namespace {

/// An eventfd: readable once written to, until read.
struct Signal {
    Fd fd{eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
    void raise() {
        uint64_t one = 1;
        [[maybe_unused]] ssize_t n = ::write(fd.get(), &one, sizeof one);
    }
    void clear() {
        uint64_t count;
        [[maybe_unused]] ssize_t n = ::read(fd.get(), &count, sizeof count);
    }
};

auto elapsed_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::steady_clock::now() - start;
}

/// Stands for sdbus-c++'s IConnection: `queued` messages wait in its own queue, where the fd
/// doesn't show them (as after a blocking call); `incoming` arrive on the fd.
struct FakeBus {
    struct PollData {
        int fd;
        short int events;
        int eventFd;
        int timeout_ms = -1;
        int getPollTimeout() const { return timeout_ms; }
    };
    Signal incoming;
    Signal woken;  // sdbus-c++'s eventFd
    int queued = 0;
    int processed = 0;
    int timeout_ms = -1;
    bool lost = false;  // the bus went away: every call fails, as sdbus-c++'s do

    PollData getEventLoopPollData() const {
        if (lost) throw std::runtime_error("Transport endpoint is not connected");
        return {incoming.fd.get(), POLLIN, woken.fd.get(), timeout_ms};
    }
    bool processPendingEvent() {
        if (lost) throw std::runtime_error("Transport endpoint is not connected");
        uint64_t count = 0;
        if (::read(incoming.fd.get(), &count, sizeof count) > 0) queued += static_cast<int>(count);
        if (queued == 0) return false;
        --queued;
        ++processed;
        return true;
    }
};

}  // namespace

TEST_CASE("event loop: with nothing ready it sleeps for the timeout and says nothing happened") {
    EventLoop loop;
    auto start = std::chrono::steady_clock::now();
    CHECK_FALSE(loop.run_once(30ms));
    CHECK(elapsed_since(start) >= 30ms);
    CHECK_FALSE(loop.run_once(0ms));  // zero: doesn't sleep
}

TEST_CASE("event loop: a ready fd runs its callback on the loop's thread, until removed") {
    EventLoop loop;
    Signal s;
    int runs = 0;
    std::thread::id ran_on;
    loop.add(s.fd.get(), EPOLLIN, [&] {
        ++runs;
        ran_on = std::this_thread::get_id();
        s.clear();
    });
    s.raise();
    CHECK(loop.run_once(1s));
    CHECK(runs == 1);
    CHECK(ran_on == std::this_thread::get_id());

    loop.remove(s.fd.get());
    s.raise();
    CHECK_FALSE(loop.run_once(0ms));
    CHECK(runs == 1);
}

TEST_CASE("event loop: a source removed by another's callback in the same turn is not run") {
    EventLoop loop;
    Signal a, b;
    int runs = 0;
    loop.add(a.fd.get(), EPOLLIN, [&] {
        ++runs;
        a.clear();
        loop.remove(b.fd.get());
    });
    loop.add(b.fd.get(), EPOLLIN, [&] {
        ++runs;
        b.clear();
        loop.remove(a.fd.get());
    });
    a.raise();
    b.raise();
    loop.run_once(1s);
    CHECK(runs == 1);
}

TEST_CASE("event loop: wake() from another thread ends the sleep at once") {
    EventLoop loop;
    auto start = std::chrono::steady_clock::now();
    std::thread waker([&] {
        std::this_thread::sleep_for(20ms);
        loop.wake();
    });
    loop.run_once(10s);
    waker.join();
    CHECK(elapsed_since(start) < 5s);
}

TEST_CASE("event queue: a value sent from another thread wakes the receiver") {
    EventLoop loop;
    EventQueue<int> queue(loop);
    std::thread sender([&] {
        std::this_thread::sleep_for(20ms);
        queue.send(7);
    });
    auto start = std::chrono::steady_clock::now();
    std::optional<int> got = queue.receive_for(10s);
    sender.join();
    CHECK(got == 7);
    CHECK(elapsed_since(start) < 5s);
}

TEST_CASE("event queue: receiving with no time left still runs the sources that are ready") {
    EventLoop loop;
    EventQueue<int> queue(loop);
    Signal s;
    loop.add(s.fd.get(), EPOLLIN, [&] {
        s.clear();
        queue.send(1);
    });
    s.raise();
    CHECK(queue.receive_for(0ms) == 1);
    CHECK_FALSE(queue.receive_for(0ms));
}

TEST_CASE("event queue: a value sent by a source ends the wait, and the timeout is kept") {
    EventLoop loop;
    EventQueue<int> queue(loop);
    auto start = std::chrono::steady_clock::now();
    CHECK_FALSE(queue.receive_for(30ms));
    CHECK(elapsed_since(start) >= 30ms);
    queue.send(2);  // on the receiving thread: no wake needed, the queue is looked at first
    CHECK(queue.receive() == 2);
}

TEST_CASE("D-Bus on the loop: messages arriving run their callbacks") {
    EventLoop loop;
    FakeBus bus;
    BusFds fds = add_bus(loop, bus);
    bus.incoming.raise();
    loop.run_once(1s);
    CHECK(bus.processed == 1);
    remove_bus(loop, fds);
}

TEST_CASE("D-Bus on the loop: messages already queued are run before sleeping, not after the next one") {
    EventLoop loop;
    FakeBus bus;
    BusFds fds = add_bus(loop, bus);
    bus.queued = 2;  // read by a blocking call: the fd shows nothing
    auto start = std::chrono::steady_clock::now();
    loop.run_once(10s);
    CHECK(bus.processed == 2);
    CHECK(elapsed_since(start) < 5s);  // it didn't sleep with them waiting
    remove_bus(loop, fds);
}

TEST_CASE("D-Bus on the loop: its own timeout limits the sleep, and its wakeup is cleared") {
    EventLoop loop;
    FakeBus bus;
    BusFds fds = add_bus(loop, bus);
    bus.timeout_ms = 20;  // a call waiting for its answer
    auto start = std::chrono::steady_clock::now();
    CHECK_FALSE(loop.run_once(10s));
    CHECK(elapsed_since(start) < 5s);

    bus.timeout_ms = -1;
    bus.woken.raise();  // sdbus-c++ saying "look at the queue"
    CHECK(loop.run_once(0ms));
    CHECK_FALSE(loop.run_once(0ms));  // read once, not ready for ever
    remove_bus(loop, fds);
}

TEST_CASE("D-Bus on the loop: a lost connection ends the loop with a message, and lets go cleanly") {
    EventLoop loop;
    FakeBus bus;
    BusFds fds = add_bus(loop, bus);
    bus.lost = true;
    CHECK_THROWS_WITH_AS(loop.run_once(0ms), doctest::Contains("D-Bus connection lost"), Error);
    remove_bus(loop, fds);  // asks the lost connection nothing (it runs in destructors)
    CHECK_FALSE(loop.run_once(0ms));
}
