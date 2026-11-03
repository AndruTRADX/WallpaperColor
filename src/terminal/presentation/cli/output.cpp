#include "terminal/presentation/cli/output.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>

#include <unistd.h>

namespace wpc {

void out(const std::string& line) {
    std::string text = line + "\n";
    if (std::fwrite(text.data(), 1, text.size(), stdout) != text.size() || std::fflush(stdout) != 0) {
        if (errno == EPIPE) std::exit(0);
        std::cerr << "cannot write to stdout: " << std::strerror(errno) << "\n";
        std::abort();
    }
}

std::string swatch(Rgb c, int width) {
    if (!isatty(STDOUT_FILENO)) return "";
    return std::format("\x1b[48;2;{};{};{}m{}\x1b[0m ", c.r, c.g, c.b, std::string(width, ' '));
}

std::string join(const std::vector<std::string>& items, std::string_view separator) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += separator;
        out += items[i];
    }
    return out;
}

}  // namespace wpc
