// Where the service's socket is. Both sides of the socket must agree on it.

#pragma once

#include <filesystem>

namespace wpc::ipc {

/// $XDG_RUNTIME_DIR/wallpapercolor.sock, or the same in /run/user/<uid>: a folder only the
/// user can enter. Without either, /tmp/wallpapercolor-<uid>.sock (the service also checks
/// who connects).
std::filesystem::path socket_path();

}  // namespace wpc::ipc
