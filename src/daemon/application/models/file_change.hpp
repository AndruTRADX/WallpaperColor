// Model: a change to a file in a watched folder, and the kinds of changes there are.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace wpc {

/// What to hear about.
enum FileChanges : uint32_t {
    FILE_OPENED = 1,
    FILE_WRITTEN = 2,  // a file finished being written, or moved in
    FILE_CREATED = 4,  // created, or moved in
    FILE_REMOVED = 8,  // removed or moved out; also the watched folder itself
};

struct FileChange {
    /// The folder it happened in.
    std::filesystem::path folder;
    /// File name inside the folder.
    std::string name;
    bool opened = false;
    bool created = false;
    /// Removed or moved out. With an empty name: the watched folder itself, whose watch has
    /// ended (watch it again once it is back).
    bool removed = false;
    /// The name is a folder.
    bool is_folder = false;
    /// Changes were lost (the kernel's queue overflowed): anything in `folder` may have
    /// changed. The name is empty.
    bool overflow = false;
};

}  // namespace wpc
