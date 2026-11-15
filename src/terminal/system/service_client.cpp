#include "terminal/system/service_client.hpp"

#include "utils/socket.hpp"

namespace wpc {

std::string UnixServiceClient::exchange(const std::string& line) {
    Fd socket = unix_connect(path_);
    set_timeouts(socket, Seconds(3), Seconds(3));
    write_all(socket, line + "\n");
    return read_line(socket);
}

}  // namespace wpc
