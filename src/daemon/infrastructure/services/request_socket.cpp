#include "daemon/infrastructure/services/request_socket.hpp"

#include <memory>
#include <thread>

#include "utils/error.hpp"
#include "utils/socket.hpp"

namespace wpc {

void UnixRequestSocket::serve(std::function<std::string(const std::string& request)> answer) {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);  // stale socket of a previous run
    auto listener = std::make_shared<Fd>(unix_listen(path_));

    std::thread([listener, answer = std::move(answer)] {
        while (true) {
            Fd client;
            try {
                client = accept_client(*listener);
            } catch (const Error&) {
                continue;
            }
            std::string line;
            try {
                set_timeouts(client, Seconds(2), Seconds(0));
                line = read_line(client);
            } catch (const Error&) {
                continue;  // nothing readable: drop the connection
            }
            std::string reply = answer(line);
            try {
                write_all(client, reply + "\n");
            } catch (const Error&) {
            }
        }
    }).detach();
}

}  // namespace wpc
