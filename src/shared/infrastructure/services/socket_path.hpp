// Service: where the service's socket is. Both sides of the socket must agree on it.

#pragma once

#include <filesystem>

namespace wpc {

class SocketPath {
public:
    /// $XDG_RUNTIME_DIR/wallpapercolor.sock (or /tmp/wallpapercolor-$USER.sock).
    std::filesystem::path path() const;
};

}  // namespace wpc
