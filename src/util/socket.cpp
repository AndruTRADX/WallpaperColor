#include "util/socket.hpp"

#include <cerrno>
#include <cstring>
#include <format>
#include <memory>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "util/error.hpp"

namespace wpc {

Fd::~Fd() {
    if (fd_ >= 0) ::close(fd_);
}

Fd& Fd::operator=(Fd&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = other.release();
    }
    return *this;
}

int Fd::release() {
    int fd = fd_;
    fd_ = -1;
    return fd;
}

[[noreturn]] static void fail_errno() {
    throw Error(os_error(errno));
}

// ---------------------------------------------------------------------------------------------
// TCP

/// Splits "host:port" or "[::1]:port" into host and port.
static std::pair<std::string, std::string> split_host_port(std::string_view address) {
    size_t colon = address.rfind(':');
    if (colon == std::string_view::npos) throw Error("invalid socket address");
    std::string_view host = address.substr(0, colon);
    std::string_view port = address.substr(colon + 1);
    if (host.starts_with('[') && host.ends_with(']')) host = host.substr(1, host.size() - 2);
    if (port.empty() || port.find_first_not_of("0123456789") != std::string_view::npos ||
        port.size() > 5 || std::stoul(std::string(port)) > 65535) {
        throw Error("invalid port value");
    }
    return {std::string(host), std::string(port)};
}

Fd tcp_connect(std::string_view address, std::chrono::milliseconds timeout) {
    auto [host, port] = split_host_port(address);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &found); rc != 0) {
        throw Error(std::format("failed to lookup address information: {}", gai_strerror(rc)));
    }
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> list(found, &freeaddrinfo);
    if (!found) throw Error("OpenRGB protocol error: no address to connect to");

    // Only the first address is tried.
    Fd fd(::socket(found->ai_family, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    if (!fd.valid()) fail_errno();

    if (::connect(fd.get(), found->ai_addr, found->ai_addrlen) != 0) {
        if (errno != EINPROGRESS) fail_errno();
        pollfd p{fd.get(), POLLOUT, 0};
        int ready = ::poll(&p, 1, static_cast<int>(timeout.count()));
        if (ready < 0) fail_errno();
        if (ready == 0) throw Error("connection timed out");
        int err = 0;
        socklen_t len = sizeof err;
        ::getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) throw Error(os_error(err));
    }

    // Back to blocking mode; reads and writes are bounded by set_timeouts().
    int flags = ::fcntl(fd.get(), F_GETFL);
    ::fcntl(fd.get(), F_SETFL, flags & ~O_NONBLOCK);
    int one = 1;
    ::setsockopt(fd.get(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

// ---------------------------------------------------------------------------------------------
// Unix sockets

static sockaddr_un unix_address(const std::filesystem::path& path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const std::string& s = path.native();
    if (s.size() >= sizeof addr.sun_path) throw Error("path must be shorter than SUN_LEN");
    std::memcpy(addr.sun_path, s.c_str(), s.size() + 1);
    return addr;
}

Fd unix_connect(const std::filesystem::path& path) {
    sockaddr_un addr = unix_address(path);
    Fd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!fd.valid()) fail_errno();
    if (::connect(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) fail_errno();
    return fd;
}

Fd unix_listen(const std::filesystem::path& path) {
    sockaddr_un addr = unix_address(path);
    Fd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!fd.valid()) fail_errno();
    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) fail_errno();
    if (::listen(fd.get(), 128) != 0) fail_errno();
    return fd;
}

Fd accept_client(const Fd& listener) {
    int fd = ::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) fail_errno();
    return Fd(fd);
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

std::string read_line(const Fd& fd) {
    std::string line;
    char c;
    while (true) {
        ssize_t n = ::recv(fd.get(), &c, 1, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            fail_errno();
        }
        if (n == 0) break;  // end of stream
        line += c;
        if (c == '\n') break;
    }
    return line;
}

}  // namespace wpc
