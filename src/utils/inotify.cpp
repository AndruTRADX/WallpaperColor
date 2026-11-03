#include "utils/inotify.hpp"

#include <cerrno>
#include <cstring>

#include <unistd.h>

#include "utils/error.hpp"

namespace wpc {

Inotify::Inotify() : fd_(inotify_init1(IN_CLOEXEC)) {
    if (!fd_.valid()) throw Error(os_error(errno));
}

int Inotify::add(const std::filesystem::path& dir, uint32_t mask) {
    int watch = inotify_add_watch(fd_.get(), dir.c_str(), mask);
    if (watch < 0) throw Error(os_error(errno));
    return watch;
}

void Inotify::remove(int watch) {
    inotify_rm_watch(fd_.get(), watch);
}

std::vector<FileEvent> Inotify::read_events() {
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
