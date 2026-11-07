#include "utils/event_loop.hpp"

#include <algorithm>
#include <cerrno>
#include <vector>

#include <sys/eventfd.h>

namespace wpc {

namespace {

/// `d` in whole milliseconds, rounded up: waking before a retry is due would only wake again.
int milliseconds_up(EventLoop::Duration d) {
    auto ms = std::chrono::ceil<std::chrono::milliseconds>(std::max(d, EventLoop::Duration::zero())).count();
    return static_cast<int>(std::min<long long>(ms, 24LL * 3600 * 1000));
}

}  // namespace

EventLoop::EventLoop()
    : epoll_(epoll_create1(EPOLL_CLOEXEC)), wake_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
    if (!epoll_.valid() || !wake_.valid()) throw Error(os_error(errno));
    epoll_event e{.events = EPOLLIN, .data = {.u64 = 0}};
    if (epoll_ctl(epoll_.get(), EPOLL_CTL_ADD, wake_.get(), &e) < 0) throw Error(os_error(errno));
}

void EventLoop::add(int fd, uint32_t events, std::function<void()> ready, Prepare prepare) {
    uint64_t id = next_id_++;
    epoll_event e{.events = events, .data = {.u64 = id}};
    if (epoll_ctl(epoll_.get(), EPOLL_CTL_ADD, fd, &e) < 0) throw Error(os_error(errno));
    sources_.emplace(id, Source{fd, std::move(ready), std::move(prepare)});
}

void EventLoop::change(int fd, uint32_t events) {
    for (const auto& [id, source] : sources_) {
        if (source.fd != fd) continue;
        epoll_event e{.events = events, .data = {.u64 = id}};
        if (epoll_ctl(epoll_.get(), EPOLL_CTL_MOD, fd, &e) < 0) throw Error(os_error(errno));
    }
}

void EventLoop::remove(int fd) {
    // An fd already closed has left epoll by itself: nothing to undo then.
    epoll_ctl(epoll_.get(), EPOLL_CTL_DEL, fd, nullptr);
    std::erase_if(sources_, [fd](const auto& entry) { return entry.second.fd == fd; });
}

std::optional<EventLoop::Duration> EventLoop::longest_sleep(std::optional<Duration> timeout) {
    std::vector<uint64_t> ids;  // a prepare may add or remove sources
    for (const auto& entry : sources_) {
        if (entry.second.prepare) ids.push_back(entry.first);
    }
    for (uint64_t id : ids) {
        auto it = sources_.find(id);
        if (it == sources_.end()) continue;
        Prepare prepare = it->second.prepare;  // a copy: the source may go while it runs
        if (std::optional<Duration> most = prepare()) timeout = timeout ? std::min(*timeout, *most) : *most;
    }
    return timeout;
}

bool EventLoop::run_once(std::optional<Duration> timeout) {
    timeout = longest_sleep(timeout);
    epoll_event ready[16];
    int n = epoll_wait(epoll_.get(), ready, 16, timeout ? milliseconds_up(*timeout) : -1);
    if (n < 0 && errno == EINTR) return false;
    if (n < 0) throw Error(os_error(errno));
    for (int i = 0; i < n; ++i) {
        uint64_t id = ready[i].data.u64;
        if (id == 0) {
            uint64_t count;
            [[maybe_unused]] ssize_t r = ::read(wake_.get(), &count, sizeof count);
            continue;
        }
        auto it = sources_.find(id);
        if (it == sources_.end()) continue;  // removed by a callback of this turn
        std::function<void()> callback = it->second.ready;  // a copy: the source may go while it runs
        callback();
    }
    return n > 0;
}

void EventLoop::wake() {
    uint64_t one = 1;
    [[maybe_unused]] ssize_t n = ::write(wake_.get(), &one, sizeof one);
}

}  // namespace wpc
