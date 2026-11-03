// What the terminal needs from picture files (the shared module's picture_files service).

#pragma once

#include <cstdint>
#include <filesystem>

#include "shared/domain/colours/picture.hpp"

namespace wpc::inline terminal {

class Pictures {
public:
    virtual ~Pictures() = default;
    /// The picture at `path`, its sides at least `min_side`. Throws wpc::Error.
    virtual Picture read(const std::filesystem::path& path, uint32_t min_side) = 0;
};

}  // namespace wpc::terminal
