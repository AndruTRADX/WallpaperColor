// What the service needs to hear about files changing in folders.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>

#include "daemon/application/models/file_change.hpp"

namespace wpc {

class FileWatch {
public:
    virtual ~FileWatch() = default;
    /// Calls `changed` (from another thread) for every change of kind `what` (FileChanges)
    /// in `folder`. Returns an id for unwatch(). Throws wpc::Error.
    virtual int watch(const std::filesystem::path& folder, uint32_t what,
                      std::function<void(const FileChange&)> changed) = 0;
    /// The same for `folder` and every folder below it, as one watch.
    virtual int watch_tree(const std::filesystem::path& folder, uint32_t what,
                           std::function<void(const FileChange&)> changed) = 0;
    virtual void unwatch(int id) = 0;
};

}  // namespace wpc
