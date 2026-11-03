#include "terminal/infrastructure/services/service_socket.hpp"

#include "utils/socket.hpp"

namespace wpc {

std::string UnixServiceSocket::exchange(const std::string& line) {
    Fd socket = unix_connect(path_);
    set_timeouts(socket, Seconds(3), Seconds(3));
    write_all(socket, line + "\n");
    return read_line(socket);
}

}  // namespace wpc
