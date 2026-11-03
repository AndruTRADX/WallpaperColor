#include "terminal/application/features/extract_colours/extract_colours.hpp"

#include "utils/error.hpp"

namespace wpc {

std::vector<PictureColours> extract_colours(Pictures& files, const std::vector<std::filesystem::path>& pictures,
                                           const ExtractConfig& config) {
    std::vector<PictureColours> out;
    for (const std::filesystem::path& picture : pictures) {
        PictureColours result{picture, std::nullopt, {}};
        try {
            result.colours = extract(files.read(picture, picture_side(config)), config);
        } catch (const Error& e) {
            result.error = e.what();
        }
        out.push_back(std::move(result));
    }
    return out;
}

}  // namespace wpc
