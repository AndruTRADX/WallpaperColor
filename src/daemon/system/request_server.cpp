#include "daemon/system/request_server.hpp"

#include <cerrno>
#include <cstdint>

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>

#include "utils/error.hpp"
#include "utils/log.hpp"
#include "utils/socket.hpp"

namespace wpc {

namespace fs = std::filesystem;

namespace {

/// A request is one short line; anything longer is not one.
constexpr size_t MAX_REQUEST = 64u << 10;

}  // namespace

UnixRequestServer::UnixRequestServer(fs::path path) : path_(std::move(path)) {
    fs::path lock_path = path_;
    lock_path += ".lock";
    lock_ = Fd(::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (!lock_.valid()) throw Error(cat("cannot open ", lock_path.string(), ": ", os_error(errno)));
    // Held while this process lives: the kernel lets it go however the process ends.
    if (::flock(lock_.get(), LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) throw Error(cat("another wallpapercolord is already running (", path_.string(), ")"));
        throw Error(cat("cannot lock ", lock_path.string(), ": ", os_error(errno)));
    }
}

UnixRequestServer::~UnixRequestServer() {
    stop();
}

void UnixRequestServer::serve(std::function<std::string(const std::string&)> answer) {
    std::error_code ignored;
    fs::remove(path_, ignored);  // a stale socket of a previous run: this process holds the lock
    listener_ = unix_listen(path_);
    // poll() says when a client waits; non-blocking, so one that left meanwhile can't block accept().
    int flags = ::fcntl(listener_.get(), F_GETFL);
    if (flags < 0 || ::fcntl(listener_.get(), F_SETFL, flags | O_NONBLOCK) != 0) throw Error(os_error(errno));
    stop_ = Fd(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
    if (!stop_.valid()) throw Error(os_error(errno));
    thread_ = std::thread([this, answer = std::move(answer)] { run(answer); });
}

void UnixRequestServer::stop() {
    if (!thread_.joinable()) return;
    uint64_t one = 1;
    [[maybe_unused]] ssize_t n = ::write(stop_.get(), &one, sizeof one);
    thread_.join();
    std::error_code ignored;
    fs::remove(path_, ignored);
}

void UnixRequestServer::run(const std::function<std::string(const std::string&)>& answer) noexcept {
    pollfd waits[2] = {{listener_.get(), POLLIN, 0}, {stop_.get(), POLLIN, 0}};
    while (true) {
        int ready = ::poll(waits, 2, -1);
        if (ready < 0) {
            if (errno != EINTR) {
                log("request socket: {}", os_error(errno));
                ::poll(&waits[1], 1, 100);  // then again, instead of turning round at full speed
            }
            continue;
        }
        if (waits[1].revents != 0) return;  // stop()
        if ((waits[0].revents & POLLIN) == 0) continue;

        int fd = ::accept4(listener_.get(), nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) {
            // Out of descriptors or memory: the client stays queued; wait a moment rather than
            // spin on it. Anything else (it left, a signal) is only that client.
            if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM) ::poll(&waits[1], 1, 100);
            continue;
        }
        Fd client(fd);
        try {
            answer_client(client, answer);
        } catch (const std::exception&) {
            // a client that didn't send a line, or left before the answer: only that one
        }
    }
}

void UnixRequestServer::answer_client(const Fd& client, const std::function<std::string(const std::string&)>& answer) {
    if (peer_uid(client) != ::getuid()) return;  // another user: not even an answer
    set_timeouts(client, Seconds(2), Seconds(2));
    std::string line = read_line(client, MAX_REQUEST);
    write_all(client, answer(line) + "\n");
}

}  // namespace wpc
