#include "utils/inotify.hpp"

#include <cerrno>
#include <cstring>

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "utils/error.hpp"

namespace wpc {

Inotify::Inotify() : fd_(inotify_init1(IN_CLOEXEC)), stop_(eventfd(0, EFD_CLOEXEC)) {
    if (!fd_.valid() || !stop_.valid()) throw Error(os_error(errno));
}

void Inotify::stop() {
    uint64_t one = 1;
    [[maybe_unused]] ssize_t n = ::write(stop_.get(), &one, sizeof one);
}

int Inotify::add(const std::filesystem::path& dir, uint32_t mask) {
    int watch = inotify_add_watch(fd_.get(), dir.c_str(), mask);
    if (watch < 0) throw Error(os_error(errno));
    return watch;
}

void Inotify::remove(int watch) {
    inotify_rm_watch(fd_.get(), watch);
}

std::optional<std::vector<FileEvent>> Inotify::read_events() {
    pollfd ready[2] = {{fd_.get(), POLLIN, 0}, {stop_.get(), POLLIN, 0}};
    int n;
    do {
        n = ::poll(ready, 2, -1);
    } while (n < 0 && errno == EINTR);
    if (n < 0) throw Error(os_error(errno));
    if (ready[1].revents) return std::nullopt;  // stopped (the eventfd stays readable)

    alignas(inotify_event) char buffer[4096];
    ssize_t length;
    do {
        length = ::read(fd_.get(), buffer, sizeof buffer);
    } while (length < 0 && errno == EINTR);
    if (length < 0) throw Error(os_error(errno));

    std::vector<FileEvent> events;
    for (ssize_t pos = 0; pos < length;) {
        inotify_event header;
        std::memcpy(&header, buffer + pos, sizeof header);
        const char* name = buffer + pos + sizeof header;
        // the name is NUL-padded to `len` bytes
        std::string text = header.len > 0 ? std::string(name, strnlen(name, header.len)) : "";
        events.push_back(FileEvent{header.wd, std::move(text), header.mask});
        pos += static_cast<ssize_t>(sizeof header + header.len);
    }
    return events;
}

}  // namespace wpc
