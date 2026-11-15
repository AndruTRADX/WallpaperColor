// The ServiceClient port: the terminal's side of the service's Unix socket. One request per
// connection: one line out, one line back.

#pragma once

#include <filesystem>

#include "terminal/app/ports.hpp"

namespace wpc {

class UnixServiceClient final : public ServiceClient {
public:
    explicit UnixServiceClient(std::filesystem::path path) : path_(std::move(path)) {}

    /// Connects, sends, waits up to 3 s for the answer.
    std::string exchange(const std::string& line) override;

private:
    std::filesystem::path path_;
};

}  // namespace wpc
