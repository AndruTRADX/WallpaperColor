// Service: the Unix socket the terminal talks to. One request per connection: one line in, one
// line out.

#pragma once

#include <filesystem>

#include "daemon/application/contracts/request_socket.hpp"

namespace wpc {

class UnixRequestSocket final : public RequestSocket {
public:
    explicit UnixRequestSocket(std::filesystem::path path) : path_(std::move(path)) {}

    /// Replaces a stale socket of a previous run. Throws wpc::Error.
    void serve(std::function<std::string(const std::string& request)> answer) override;

private:
    std::filesystem::path path_;
};

}  // namespace wpc
