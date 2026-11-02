// A queue that threads use to hand values to each other, like Rust's std::sync::mpsc channel:
// any thread can send(); one thread receives, sleeping until something arrives.

#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace wpc {

template <typename T>
class Channel {
public:
    void send(T value) {
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(std::move(value));
        }
        ready_.notify_one();
    }

    /// Waits as long as it takes for the next value.
    T receive() {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [&] { return !queue_.empty(); });
        return pop();
    }

    /// Waits at most `timeout`; nullopt if nothing arrived in time.
    std::optional<T> receive_for(std::chrono::steady_clock::duration timeout) {
        std::unique_lock lock(mutex_);
        if (!ready_.wait_for(lock, timeout, [&] { return !queue_.empty(); })) return std::nullopt;
        return pop();
    }

private:
    T pop() {
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<T> queue_;
};

}  // namespace wpc
