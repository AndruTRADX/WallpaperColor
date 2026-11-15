// Fd: owns a file descriptor and closes it when it goes out of scope (RAII). Move-only, so a
// descriptor has exactly one owner and is closed exactly once.

#pragma once

namespace wpc {

class Fd {
public:
    Fd() = default;
    explicit Fd(int fd) noexcept : fd_(fd) {}
    ~Fd();
    Fd(Fd&& other) noexcept : fd_(other.release()) {}
    Fd& operator=(Fd&& other) noexcept;
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    /// Gives the descriptor up without closing it.
    int release() noexcept;

private:
    int fd_ = -1;
};

}  // namespace wpc
