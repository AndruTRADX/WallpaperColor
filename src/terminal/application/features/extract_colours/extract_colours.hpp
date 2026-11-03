// Feature: the colours that would be taken from pictures (`wallpapercolor extract`, the terminal
// UI's preview). Nothing is painted.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "shared/domain/colours/extract.hpp"
#include "terminal/application/contracts/external/pictures.hpp"

namespace wpc {

struct PictureColours {
    std::filesystem::path picture;
    /// The colours, or why the picture couldn't be read.
    std::optional<Extraction> colours;
    std::string error;
};

std::vector<PictureColours> extract_colours(Pictures& files, const std::vector<std::filesystem::path>& pictures,
                                           const ExtractConfig& config);

}  // namespace wpc
