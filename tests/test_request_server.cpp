// The service's request socket (daemon/system/request_server.hpp) on a temporary path: a line
// in, the answer out; only one service per socket; stopping it stops its thread.

#include <doctest/doctest.h>

#include <atomic>
#include <cstdlib>
#include <thread>

#include "daemon/system/request_server.hpp"
#include "utils/error.hpp"
#include "utils/socket.hpp"

using namespace wpc;
namespace fs = std::filesystem;

namespace {

struct TempFolder {
    fs::path path;
    TempFolder() {
        std::string pattern = (fs::temp_directory_path() / "wpc-socket-XXXXXX").string();
        path = ::mkdtemp(pattern.data());
    }
    ~TempFolder() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

/// One request, as the terminal sends it.
std::string ask(const fs::path& socket, const std::string& line) {
    Fd fd = unix_connect(socket);
    set_timeouts(fd, Seconds(3), Seconds(3));
    write_all(fd, line + "\n");
    return read_line(fd);
}

}  // namespace

TEST_CASE("request server: a line in, its answer out, for its own user only") {
    TempFolder temp;
    fs::path socket = temp.path / "wallpapercolor.sock";
    UnixRequestServer server(socket);
    server.serve([](const std::string& line) { return "you said " + line.substr(0, line.size() - 1); });
    CHECK(ask(socket, "status") == "you said status\n");
    CHECK(ask(socket, "pause") == "you said pause\n");
    CHECK((fs::status(socket).permissions() & fs::perms::all) == (fs::perms::owner_read | fs::perms::owner_write));

    // A client that never sends anything doesn't keep the others waiting for ever.
    Fd silent = unix_connect(socket);
    CHECK(ask(socket, "apply") == "you said apply\n");
}

TEST_CASE("request server: a second service can't take the first one's socket") {
    TempFolder temp;
    fs::path socket = temp.path / "wallpapercolor.sock";
    UnixRequestServer first(socket);
    first.serve([](const std::string&) { return std::string("first"); });
    CHECK_THROWS_WITH_AS(UnixRequestServer second(socket), doctest::Contains("already running"), Error);
    CHECK(ask(socket, "x") == "first\n");
}

TEST_CASE("request server: once stopped, it answers nothing and its socket is gone") {
    TempFolder temp;
    fs::path socket = temp.path / "wallpapercolor.sock";
    std::atomic<int> answered = 0;
    {
        UnixRequestServer server(socket);
        server.serve([&](const std::string&) {
            ++answered;
            return std::string("ok");
        });
        CHECK(ask(socket, "x") == "ok\n");
        server.stop();
        CHECK_FALSE(fs::exists(socket));
        CHECK_THROWS_AS(ask(socket, "x"), Error);
    }
    CHECK(answered == 1);
    UnixRequestServer again(socket);  // the lock went with the first one
}
