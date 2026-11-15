#include "utils/fd.hpp"

#include <unistd.h>

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

int Fd::release() noexcept {
    int fd = fd_;
    fd_ = -1;
    return fd;
}

}  // namespace wpc
