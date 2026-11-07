// Service: hearing about files changing in folders, through one inotify instance waited for in
// the service's loop (no thread of its own). Several watches can share a folder: each gets only
// the kinds of changes it asked for.

#pragma once

#include <memory>

#include "daemon/application/contracts/file_watch.hpp"
#include "utils/event_loop.hpp"

namespace wpc {

class InotifyFileWatch final : public FileWatch {
public:
    /// Waits for changes in `loop`, which runs the callbacks. Throws wpc::Error.
    explicit InotifyFileWatch(EventLoop& loop);
    /// No callback runs after this.
    ~InotifyFileWatch() override;

    int watch(const std::filesystem::path& folder, uint32_t what, std::function<void(const FileChange&)> changed) override;
    int watch_tree(const std::filesystem::path& folder, uint32_t what,
                   std::function<void(const FileChange&)> changed) override;
    void unwatch(int id) override;

private:
    /// Reads what the kernel has and calls the watches that asked for it.
    void deliver();

    struct State;
    EventLoop& loop_;
    std::unique_ptr<State> state_;
};

}  // namespace wpc
