// Small wrappers over POSIX sockets: TCP (the OpenRGB driver) and Unix sockets (between the
// terminal and the service). Every failure throws wpc::Error.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>

#include "utils/fd.hpp"

namespace wpc {

using Seconds = std::chrono::seconds;

/// Connects to "host:port" (or "[ipv6]:port"). Every address the host has is tried in turn
/// ("localhost" is often ::1 first, while the server only listens on 127.0.0.1); `timeout` is
/// for all of them together.
Fd tcp_connect(std::string_view address, std::chrono::milliseconds timeout);

/// Connects to the Unix socket at `path`.
Fd unix_connect(const std::filesystem::path& path);

/// Listens on the Unix socket at `path`, which must not exist. Only its owner may connect
/// (mode 0600), whatever the umask.
Fd unix_listen(const std::filesystem::path& path);

/// Waits for the next client of a listening socket.
Fd accept_client(const Fd& listener);

/// The user id of the process at the other end of a Unix socket.
uid_t peer_uid(const Fd& fd);

/// Read and write timeouts: a blocked read or write fails after this long. Zero: no limit.
void set_timeouts(const Fd& fd, Seconds read, Seconds write);

/// Sends every byte.
void write_all(const Fd& fd, std::span<const uint8_t> data);
void write_all(const Fd& fd, std::string_view text);

/// Fills `buffer` completely, or throws (also when the other side closes first).
void read_exact(const Fd& fd, std::span<uint8_t> buffer);

/// Reads up to and including the next '\n' (or to the end of the stream), for a connection
/// that carries one line: bytes that arrive after the '\n' in the same read are dropped. A line
/// longer than `max` bytes is an error, so whoever sends it can't make it grow without end.
std::string read_line(const Fd& fd, size_t max = 16u << 20);

}  // namespace wpc
