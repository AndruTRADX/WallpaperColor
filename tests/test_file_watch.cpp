// The real file watch (inotify) on a temporary folder: what the fake in test_service.cpp
// stands for.

#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>  // mkdtemp
#include <fstream>
#include <thread>

#include "daemon/infrastructure/services/file_watch.hpp"
#include "utils/event_queue.hpp"

using namespace wpc;
namespace fs = std::filesystem;

namespace {

/// A new empty folder, removed at the end.
struct TempFolder {
    fs::path path;
    TempFolder() {
        std::string pattern = (fs::temp_directory_path() / "wpc-watch-XXXXXX").string();
        path = mkdtemp(pattern.data());
    }
    ~TempFolder() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

/// The changes a watch heard, in order. Waiting for one runs `loop`, which reads inotify.
struct Heard {
    explicit Heard(EventLoop& loop) : changes(loop) {}
    EventQueue<FileChange> changes;
    std::function<void(const FileChange&)> callback() {
        return [this](const FileChange& c) { changes.send(c); };
    }
    /// The first change that `wanted` accepts, within `limit`.
    std::optional<FileChange> wait_for(const std::function<bool(const FileChange&)>& wanted,
                                       std::chrono::milliseconds limit = std::chrono::seconds(2)) {
        auto until = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < until) {
            std::optional<FileChange> c = changes.receive_for(until - std::chrono::steady_clock::now());
            if (c && wanted(*c)) return c;
        }
        return std::nullopt;
    }
};

}  // namespace

TEST_CASE("file watch: a folder created or moved in is a created folder") {
    TempFolder temp;
    fs::create_directory(temp.path / "outside");
    EventLoop loop;
    Heard heard(loop);  // before the watch: it is still there while the watch stops
    InotifyFileWatch files(loop);
    files.watch_tree(temp.path, FILE_CREATED | FILE_REMOVED, heard.callback());

    fs::create_directory(temp.path / "made");
    auto made = heard.wait_for([](const FileChange& c) { return c.name == "made"; });
    REQUIRE(made);
    CHECK(made->created);
    CHECK(made->is_folder);

    fs::create_directory(temp.path / "sub");
    heard.wait_for([](const FileChange& c) { return c.name == "sub"; });
    fs::rename(temp.path / "outside", temp.path / "moved");
    auto moved = heard.wait_for([](const FileChange& c) { return c.name == "moved"; });
    REQUIRE(moved);
    CHECK(moved->created);
    CHECK(moved->is_folder);
}

TEST_CASE("file watch: the watched folder removed says so, and can be watched again once back") {
    TempFolder temp;
    fs::path slides = temp.path / "slides";
    fs::create_directory(slides);
    EventLoop loop;
    Heard heard(loop);
    Heard again(loop);
    InotifyFileWatch files(loop);
    int id = files.watch_tree(slides, FILE_OPENED | FILE_CREATED | FILE_REMOVED, heard.callback());

    fs::remove(slides);
    auto gone = heard.wait_for([](const FileChange& c) { return c.removed && c.name.empty(); });
    REQUIRE(gone);
    CHECK(gone->folder == slides);
    CHECK(gone->is_folder);
    files.unwatch(id);  // its watch already ended: nothing to stop, nothing breaks

    fs::create_directory(slides);
    files.watch_tree(slides, FILE_OPENED | FILE_CREATED | FILE_REMOVED, again.callback());
    std::ofstream(slides / "red.png") << "x";
    auto created = again.wait_for([](const FileChange& c) { return c.name == "red.png"; });
    REQUIRE(created);
    CHECK(created->created);
    CHECK_FALSE(heard.wait_for([](const FileChange& c) { return c.name == "red.png"; },
                               std::chrono::milliseconds(200)));  // the old one is over
}

TEST_CASE("file watch: changes are heard on the thread that runs the loop, with no thread of its own") {
    TempFolder temp;
    EventLoop loop;
    std::optional<std::thread::id> heard_on;
    InotifyFileWatch files(loop);
    files.watch(temp.path, FILE_CREATED, [&](const FileChange&) { heard_on = std::this_thread::get_id(); });

    std::ofstream(temp.path / "red.png") << "x";
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(heard_on);  // nothing runs until the loop does
    loop.run_once(std::chrono::seconds(2));
    CHECK(heard_on == std::this_thread::get_id());
}
