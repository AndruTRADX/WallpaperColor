#include "protocol/socket_path.hpp"

#include <optional>
#include <string>

#include <unistd.h>

#include "utils/files.hpp"

namespace wpc::ipc {

std::filesystem::path socket_path() {
    if (std::optional<std::filesystem::path> runtime = runtime_dir()) return *runtime / "wallpapercolor.sock";
    // The user id, not $USER: the name of the file must not come from the environment.
    return std::filesystem::path("/tmp") / ("wallpapercolor-" + std::to_string(::getuid()) + ".sock");
}

}  // namespace wpc::ipc
