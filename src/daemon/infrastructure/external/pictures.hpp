// Adapter: the shared module's picture_files service as the service's Pictures.

#pragma once

#include "daemon/application/contracts/external/pictures.hpp"
#include "shared/infrastructure/services/picture_files.hpp"

namespace wpc::inline daemon {

class PictureFilesAdapter final : public Pictures {
public:
    Picture read(const std::filesystem::path& path, uint32_t min_side) override { return files_.read(path, min_side); }
    std::optional<int64_t> modified_time(const std::filesystem::path& path) override { return files_.modified_time(path); }

private:
    PictureFiles files_;
};

}  // namespace wpc::daemon
