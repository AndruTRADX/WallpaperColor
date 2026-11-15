#include "utils/files.hpp"

#include <cerrno>
#include <cstdlib>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils/error.hpp"
#include "utils/fd.hpp"

namespace wpc {

namespace fs = std::filesystem;

namespace {

/// "path: No such file or directory (os error 2)".
[[noreturn]] void fail(const fs::path& path, int code) {
    throw Error(cat(path.string(), ": ", os_error(code)));
}

void write_whole(const Fd& fd, std::string_view text, const fs::path& path) {
    while (!text.empty()) {
        ssize_t n = ::write(fd.get(), text.data(), text.size());
        if (n < 0) {
            if (errno == EINTR) continue;
            fail(path, errno);
        }
        text.remove_prefix(static_cast<size_t>(n));
    }
}

}  // namespace

std::optional<std::string> read_file(const fs::path& path, size_t max) {
    // O_NONBLOCK: opening a FIFO doesn't wait for a writer (it is refused just below).
    Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK));
    if (!fd.valid()) {
        if (errno == ENOENT) return std::nullopt;
        fail(path, errno);
    }
    struct stat info {};
    if (::fstat(fd.get(), &info) != 0) fail(path, errno);
    if (!S_ISREG(info.st_mode)) throw Error(cat(path.string(), ": not a regular file"));
    if (static_cast<uint64_t>(info.st_size) > max) {
        throw Error(cat(path.string(), ": bigger than ", max / (1u << 20), " MB"));
    }

    std::string text(static_cast<size_t>(info.st_size), '\0');
    size_t filled = 0;
    while (true) {
        if (filled == text.size()) {
            if (text.size() >= max) break;  // it grew while being read: what fits is kept
            text.resize(std::min(max, text.size() + 4096));
        }
        ssize_t n = ::read(fd.get(), text.data() + filled, text.size() - filled);
        if (n < 0) {
            if (errno == EINTR) continue;
            fail(path, errno);
        }
        if (n == 0) break;
        filled += static_cast<size_t>(n);
    }
    text.resize(filled);
    return text;
}

void write_file_atomically(const fs::path& path, std::string_view text) {
    // The real file: a symlink is followed, so the link itself is left as it is.
    std::error_code ec;
    fs::path target = fs::weakly_canonical(path, ec);
    if (ec) target = path;
    fs::path folder = target.has_parent_path() ? target.parent_path() : fs::path(".");
    fs::create_directories(folder, ec);
    if (ec) fail(folder, ec.value());

    // Keep the permissions of the file it replaces (a new one gets rw-r--r--).
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    struct stat old {};
    if (::stat(target.c_str(), &old) == 0) mode = old.st_mode & 07777;

    std::string temporary = (folder / ("." + target.filename().string() + ".XXXXXX")).string();
    Fd fd(::mkostemp(temporary.data(), O_CLOEXEC));
    if (!fd.valid()) fail(folder, errno);
    try {
        write_whole(fd, text, temporary);
        if (::fchmod(fd.get(), mode) != 0) fail(temporary, errno);
        if (::fsync(fd.get()) != 0) fail(temporary, errno);
        fd = Fd();  // closed before it replaces the old one
        if (::rename(temporary.c_str(), target.c_str()) != 0) fail(target, errno);
    } catch (...) {
        ::unlink(temporary.c_str());
        throw;
    }
    // The rename itself on the disk too.
    Fd dir(::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (dir.valid()) ::fsync(dir.get());
}

std::optional<fs::path> config_home() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && fs::path(xdg).is_absolute()) return fs::path(xdg);
    const char* home = std::getenv("HOME");
    if (home && fs::path(home).is_absolute()) return fs::path(home) / ".config";
    return std::nullopt;
}

std::optional<fs::path> program_folder() {
    std::error_code ec;
    fs::path program = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return std::nullopt;
    return program.parent_path();
}

std::optional<fs::path> runtime_dir() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && fs::path(xdg).is_absolute()) return fs::path(xdg);
    fs::path own = fs::path("/run/user") / std::to_string(::getuid());
    std::error_code ec;
    if (fs::is_directory(own, ec)) return own;
    return std::nullopt;
}

}  // namespace wpc
