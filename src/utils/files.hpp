// Reading and writing whole files safely.

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace wpc {

/// The whole file at `path`; nullopt if there is none. Throws wpc::Error for anything else: no
/// permission, not a regular file (a FIFO would block the reader for ever), or bigger than
/// `max` bytes.
std::optional<std::string> read_file(const std::filesystem::path& path, size_t max = 16u << 20);

/// Replaces the file at `path` with `text` so that a reader sees the old file or the new one,
/// never half of one, even after a crash: the text goes to a new file in the same folder, is
/// flushed to the disk, then renamed over the old one. A symlink stays a symlink (the file it
/// points to is replaced: dotfiles managers keep working) and the file keeps its permissions.
/// Throws wpc::Error.
void write_file_atomically(const std::filesystem::path& path, std::string_view text);

/// Where the user's settings go: $XDG_CONFIG_HOME, else ~/.config. nullopt without either.
std::optional<std::filesystem::path> config_home();

/// The folder of the running program (from /proc/self/exe); nullopt if it can't be read.
std::optional<std::filesystem::path> program_folder();

/// Where the user's sockets go: $XDG_RUNTIME_DIR, else /run/user/<uid> when it exists (both
/// are private to the user). nullopt without either.
std::optional<std::filesystem::path> runtime_dir();

}  // namespace wpc
