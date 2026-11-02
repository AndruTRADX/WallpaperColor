// Linux inotify: the kernel tells us when files in watched folders are opened, written,
// created or moved. Reading blocks until something happens, so a waiting thread uses no CPU.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <sys/inotify.h>

#include "util/socket.hpp"

namespace wpc {

struct FileEvent {
    /// Which watch it came from (as returned by Inotify::add).
    int watch = -1;
    /// File name inside the watched folder ("" for the folder itself).
    std::string name;
    /// IN_OPEN, IN_CREATE, IN_ISDIR…
    uint32_t mask = 0;
};

class Inotify {
public:
    /// Throws wpc::Error.
    Inotify();

    /// Starts watching `dir` for `mask` events; returns the watch id. Throws wpc::Error.
    /// Watching the same folder again returns the same id and replaces the mask.
    int add(const std::filesystem::path& dir, uint32_t mask);
    void remove(int watch);

    /// Waits for the next events. Throws wpc::Error.
    std::vector<FileEvent> read_events();

private:
    Fd fd_;
};

}  // namespace wpc
