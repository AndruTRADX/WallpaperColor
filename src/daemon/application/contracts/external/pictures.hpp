// What the service needs from picture files (the shared module's picture_files service).

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include "shared/domain/colours/picture.hpp"

namespace wpc::inline daemon {

class Pictures {
public:
    virtual ~Pictures() = default;
    /// The picture at `path`, its sides at least `min_side`. Throws wpc::Error.
    virtual Picture read(const std::filesystem::path& path, uint32_t min_side) = 0;
    /// When the file was last changed; nullopt if it can't be read.
    virtual std::optional<int64_t> modified_time(const std::filesystem::path& path) = 0;
};

}  // namespace wpc::daemon
