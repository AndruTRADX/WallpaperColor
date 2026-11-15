#include "utils/socket.hpp"

#include <cerrno>
#include <cstring>
#include <memory>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "utils/error.hpp"

namespace wpc {

namespace {

[[noreturn]] void fail_errno() {
    throw Error(os_error(errno));
}

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

/// Splits "host:port" or "[::1]:port" into host and port.
std::pair<std::string, std::string> split_host_port(std::string_view address) {
    size_t colon = address.rfind(':');
    if (colon == std::string_view::npos) throw Error("invalid socket address");
    std::string_view host = address.substr(0, colon);
    std::string_view port = address.substr(colon + 1);
    if (host.starts_with('[') && host.ends_with(']')) host = host.substr(1, host.size() - 2);
    if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string_view::npos ||
        std::stoul(std::string(port)) > 65535) {
        throw Error("invalid port value");
    }
    return {std::string(host), std::string(port)};
}

/// Waits until `fd` has `events`, or `deadline`. False when the time ran out.
bool wait_for(int fd, short events, Clock::time_point deadline) {
    while (true) {
        auto left = std::chrono::ceil<milliseconds>(deadline - Clock::now());
        if (left <= milliseconds::zero()) return false;
        pollfd p{fd, events, 0};
        int ready = ::poll(&p, 1, static_cast<int>(left.count()));
        if (ready > 0) return true;
        if (ready == 0) return false;
        if (errno != EINTR) fail_errno();
    }
}

/// Connects to one resolved address, giving up at `deadline`.
Fd connect_to(const addrinfo& address, Clock::time_point deadline) {
    Fd fd(::socket(address.ai_family, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    if (!fd.valid()) fail_errno();
    if (::connect(fd.get(), address.ai_addr, address.ai_addrlen) != 0) {
        if (errno != EINPROGRESS) fail_errno();
        if (!wait_for(fd.get(), POLLOUT, deadline)) throw Error("connection timed out");
        int err = 0;
        socklen_t len = sizeof err;
        if (::getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &err, &len) != 0) fail_errno();
        if (err != 0) throw Error(os_error(err));
    }
    // Back to blocking mode: reads and writes are bounded by set_timeouts().
    int flags = ::fcntl(fd.get(), F_GETFL);
    if (flags < 0 || ::fcntl(fd.get(), F_SETFL, flags & ~O_NONBLOCK) < 0) fail_errno();
    int one = 1;
    ::setsockopt(fd.get(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);  // small packets go at once
    return fd;
}

sockaddr_un unix_address(const std::filesystem::path& path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const std::string& s = path.native();
    if (s.size() >= sizeof addr.sun_path) throw Error("path must be shorter than SUN_LEN");
    std::memcpy(addr.sun_path, s.c_str(), s.size() + 1);
    return addr;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// TCP

Fd tcp_connect(std::string_view address, std::chrono::milliseconds timeout) {
    auto [host, port] = split_host_port(address);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (int rc = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &found); rc != 0) {
        throw Error(cat("failed to lookup address information: ", ::gai_strerror(rc)));
    }
    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> list(found, &::freeaddrinfo);

    Clock::time_point deadline = Clock::now() + timeout;
    std::string last_problem = "no address to connect to";
    for (const addrinfo* a = found; a != nullptr; a = a->ai_next) {
        try {
            return connect_to(*a, deadline);
        } catch (const Error& e) {
            last_problem = e.what();
        }
        if (Clock::now() >= deadline) break;
    }
    throw Error(last_problem);
}

// ---------------------------------------------------------------------------------------------
// Unix sockets

Fd unix_connect(const std::filesystem::path& path) {
    sockaddr_un addr = unix_address(path);
    Fd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!fd.valid()) fail_errno();
    if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) fail_errno();
    return fd;
}

Fd unix_listen(const std::filesystem::path& path) {
    sockaddr_un addr = unix_address(path);
    Fd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!fd.valid()) fail_errno();
    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) fail_errno();
    // Connecting needs write permission on the socket's file: only its owner.
    if (::chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) fail_errno();
    if (::listen(fd.get(), 16) != 0) fail_errno();
    return fd;
}

Fd accept_client(const Fd& listener) {
    int fd = ::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) fail_errno();
    return Fd(fd);
}

uid_t peer_uid(const Fd& fd) {
    ucred credentials{};
    socklen_t length = sizeof credentials;
    if (::getsockopt(fd.get(), SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) fail_errno();
    return credentials.uid;
}

// ---------------------------------------------------------------------------------------------
// Reading and writing

void set_timeouts(const Fd& fd, Seconds read, Seconds write) {
    timeval r{static_cast<time_t>(read.count()), 0};
    timeval w{static_cast<time_t>(write.count()), 0};
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &r, sizeof r) != 0) fail_errno();
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_SNDTIMEO, &w, sizeof w) != 0) fail_errno();
}

void write_all(const Fd& fd, std::span<const uint8_t> data) {
    while (!data.empty()) {
        ssize_t n = ::send(fd.get(), data.data(), data.size(), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            fail_errno();
        }
        if (n == 0) throw Error("failed to write whole buffer");
        data = data.subspan(static_cast<size_t>(n));
    }
}

void write_all(const Fd& fd, std::string_view text) {
    write_all(fd, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

void read_exact(const Fd& fd, std::span<uint8_t> buffer) {
    while (!buffer.empty()) {
        ssize_t n = ::recv(fd.get(), buffer.data(), buffer.size(), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            fail_errno();
        }
        if (n == 0) throw Error("failed to fill whole buffer");
        buffer = buffer.subspan(static_cast<size_t>(n));
    }
}

std::string read_line(const Fd& fd, size_t max) {
    std::string line;
    char buffer[4096];
    while (true) {
        ssize_t n = ::recv(fd.get(), buffer, sizeof buffer, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            fail_errno();
        }
        if (n == 0) return line;  // end of stream
        // Only the new bytes are searched: each byte is looked at once.
        std::string_view chunk(buffer, static_cast<size_t>(n));
        size_t end = chunk.find('\n');
        size_t take = end == std::string_view::npos ? chunk.size() : end + 1;
        if (line.size() + take > max) throw Error(cat("line longer than ", max, " bytes"));
        line.append(chunk.substr(0, take));
        if (end != std::string_view::npos) return line;
    }
}

}  // namespace wpc
