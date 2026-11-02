// The service's log: one line per message on stderr (journald collects it).

#pragma once

#include <cstdio>
#include <format>
#include <string>

namespace wpc {

template <typename... Args>
void log(std::format_string<Args...> format, Args&&... args) {
    std::string line = std::format(format, std::forward<Args>(args)...) + "\n";
    std::fwrite(line.data(), 1, line.size(), stderr);
}

}  // namespace wpc
