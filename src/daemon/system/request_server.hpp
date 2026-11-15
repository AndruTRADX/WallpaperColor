// The RequestServer port: the Unix socket the terminal talks to. One request per connection:
// one line in, one line out.
//
// It answers on a thread of its own, so a request still gets an answer ("busy") while the
// service's loop is in a fade or waiting for Plasma. That thread is stopped and joined by
// stop() (and when the server goes): it never outlives what `answer` uses.
//
// Only one service runs per user: the server holds a lock next to the socket, and a second
// service stops at once ("already running") instead of taking the first one's socket.
// Only the user can talk to it: the socket is 0600 in the user's own runtime folder, and every
// client's user id is checked. A client gets 2 s to send its line (at most 64 KB) and 2 s to
// take the answer, so none can keep the thread waiting.

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include "daemon/service/ports.hpp"
#include "utils/fd.hpp"

namespace wpc {

class UnixRequestServer final : public RequestServer {
public:
    /// Takes the lock of the socket at `path`. Throws wpc::Error when another service has it.
    explicit UnixRequestServer(std::filesystem::path path);
    ~UnixRequestServer() override;
    UnixRequestServer(const UnixRequestServer&) = delete;
    UnixRequestServer& operator=(const UnixRequestServer&) = delete;

    /// Replaces a stale socket of a previous run. Throws wpc::Error.
    void serve(std::function<std::string(const std::string& request)> answer) override;
    void stop() override;

private:
    void run(const std::function<std::string(const std::string&)>& answer) noexcept;
    /// Answers one client: an exception here is about that client only.
    void answer_client(const Fd& client, const std::function<std::string(const std::string&)>& answer);

    std::filesystem::path path_;
    Fd lock_;
    Fd listener_;
    Fd stop_;  // an eventfd: written to end the thread
    std::thread thread_;
};

}  // namespace wpc
