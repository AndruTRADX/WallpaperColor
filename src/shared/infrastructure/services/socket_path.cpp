#include "shared/infrastructure/services/socket_path.hpp"

#include <cstdlib>
#include <string>

namespace wpc {

namespace fs = std::filesystem;

fs::path SocketPath::path() const {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime && fs::path(runtime).is_absolute()) return fs::path(runtime) / "wallpapercolor.sock";
    const char* user = std::getenv("USER");
    return fs::path("/tmp") / (std::string("wallpapercolor-") + (user ? user : "user") + ".sock");
}

}  // namespace wpc
