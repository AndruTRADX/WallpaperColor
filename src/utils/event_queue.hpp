// A queue of values for the thread that runs an EventLoop: the loop's sources, and any other
// thread, send(); the thread that created the queue receives, sleeping in the loop (and running
// its sources) until something arrives.

#pragma once

#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

#include "utils/event_loop.hpp"

namespace wpc {

template <typename T>
class EventQueue {
public:
    explicit EventQueue(EventLoop& loop) : loop_(loop), receiver_(std::this_thread::get_id()) {}

    void send(T value) {
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(std::move(value));
        }
        // The receiving thread itself checks the queue before it sleeps again.
        if (std::this_thread::get_id() != receiver_) loop_.wake();
    }

    /// Waits as long as it takes for the next value.
    T receive() {
        while (true) {
            if (std::optional<T> value = pop()) return std::move(*value);
            loop_.run_once(std::nullopt);
        }
    }

    /// Waits at most `timeout`; nullopt if nothing arrived in time. Zero still runs the sources
    /// that are ready, without sleeping.
    std::optional<T> receive_for(EventLoop::Duration timeout) {
        auto until = std::chrono::steady_clock::now() + timeout;
        while (true) {
            if (std::optional<T> value = pop()) return value;
            EventLoop::Duration left = until - std::chrono::steady_clock::now();
            if (left <= EventLoop::Duration::zero()) {
                loop_.run_once(EventLoop::Duration::zero());
                return pop();
            }
            loop_.run_once(left);
        }
    }

private:
    std::optional<T> pop() {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    EventLoop& loop_;
    std::thread::id receiver_;
    std::mutex mutex_;  // other threads send too
    std::deque<T> queue_;
};

}  // namespace wpc
