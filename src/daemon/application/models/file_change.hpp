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
    FILE_CREATED = 4,
};

struct FileChange {
    /// The folder it happened in.
    std::filesystem::path folder;
    /// File name inside the folder.
    std::string name;
    bool opened = false;
    bool created = false;
    /// The name is a folder.
    bool is_folder = false;
};

}  // namespace wpc
