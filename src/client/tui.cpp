#include "client/tui.hpp"

#include "util/error.hpp"

namespace wpc {

void run_tui(const std::filesystem::path&) {
    throw Error("the terminal UI is not ported yet");
}

}  // namespace wpc
