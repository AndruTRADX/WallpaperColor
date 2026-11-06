// Service: hearing about files changing in folders, through one inotify instance and one
// thread that sleeps until the kernel has something. Several watches can share a folder: each
// gets only the kinds of changes it asked for.

#pragma once

#include <memory>
#include <thread>

#include "daemon/application/contracts/file_watch.hpp"

namespace wpc {

class InotifyFileWatch final : public FileWatch {
public:
    /// Starts the thread. Throws wpc::Error.
    InotifyFileWatch();
    /// Stops the thread and waits for it: no callback runs after this returns.
    ~InotifyFileWatch() override;

    int watch(const std::filesystem::path& folder, uint32_t what, std::function<void(const FileChange&)> changed) override;
    int watch_tree(const std::filesystem::path& folder, uint32_t what,
                   std::function<void(const FileChange&)> changed) override;
    void unwatch(int id) override;

private:
    struct State;
    std::shared_ptr<State> state_;  // shared with the thread
    std::thread thread_;
};

}  // namespace wpc
