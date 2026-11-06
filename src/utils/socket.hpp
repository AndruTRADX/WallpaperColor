// Small RAII wrappers over POSIX sockets: TCP (OpenRGB) and Unix sockets (service IPC).
// Every failure throws wpc::Error.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace wpc {

/// Owns a file descriptor and closes it when it goes out of scope.
class Fd {
public:
    Fd() = default;
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd();
    Fd(Fd&& other) noexcept : fd_(other.release()) {}
    Fd& operator=(Fd&& other) noexcept;
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;

    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    int release();

private:
    int fd_ = -1;
};

using Seconds = std::chrono::seconds;

/// Connects to "host:port" (or "[ipv6]:port"), giving up after `timeout`.
Fd tcp_connect(std::string_view address, std::chrono::milliseconds timeout);

/// Connects to the Unix socket at `path`.
Fd unix_connect(const std::filesystem::path& path);

/// Listens on the Unix socket at `path` (which must not exist).
Fd unix_listen(const std::filesystem::path& path);

/// Waits for the next client of a listening socket.
Fd accept_client(const Fd& listener);

/// Read and write timeouts: a blocked read or write fails after this long.
void set_timeouts(const Fd& fd, Seconds read, Seconds write);

/// Sends every byte.
void write_all(const Fd& fd, std::span<const uint8_t> data);
void write_all(const Fd& fd, std::string_view text);

/// Fills `buffer` completely, or throws (also when the other side closes first).
void read_exact(const Fd& fd, std::span<uint8_t> buffer);

/// Reads up to and including the next '\n' (or to the end of the stream). A line longer than
/// `max` bytes is an error: whoever sends it can't make it grow without end.
std::string read_line(const Fd& fd, size_t max = 16u << 20);

}  // namespace wpc
