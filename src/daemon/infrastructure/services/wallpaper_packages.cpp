#include "daemon/infrastructure/services/wallpaper_packages.hpp"

#include <string>
#include <vector>

namespace wpc {

namespace fs = std::filesystem;

std::optional<fs::path> PlasmaPackages::picture(const fs::path& path, bool dark) {
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) return path;

    std::vector<std::string> subdirs = dark ? std::vector<std::string>{"images_dark", "images"}
                                            : std::vector<std::string>{"images"};
    for (const std::string& sub : subdirs) {
        fs::directory_iterator it(path / "contents" / sub, ec);
        if (ec) continue;  // no such folder
        std::optional<fs::path> biggest;
        uintmax_t biggest_size = 0;
        for (const fs::directory_entry& entry : it) {
            std::error_code e;
            if (entry.symlink_status(e).type() != fs::file_type::regular) continue;
            uintmax_t size = entry.file_size(e);
            if (e) size = 0;
            // On a tie the last one wins (`>=`).
            if (!biggest || size >= biggest_size) {
                biggest = entry.path();
                biggest_size = size;
            }
        }
        if (biggest) return biggest;
    }
    return std::nullopt;
}

}  // namespace wpc
