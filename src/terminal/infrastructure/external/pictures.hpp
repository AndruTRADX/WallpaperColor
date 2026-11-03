// Adapter: the shared module's picture_files service as the terminal's Pictures.

#pragma once

#include "shared/infrastructure/services/picture_files.hpp"
#include "terminal/application/contracts/external/pictures.hpp"

namespace wpc::inline terminal {

class PictureFilesAdapter final : public Pictures {
public:
    Picture read(const std::filesystem::path& path, uint32_t min_side) override { return files_.read(path, min_side); }

private:
    PictureFiles files_;
};

}  // namespace wpc::terminal
