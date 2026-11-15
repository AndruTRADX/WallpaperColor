// Reading and writing whole files (utils/files.hpp): what the settings, the config editor and
// the colour scheme go through.

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>

#include <sys/stat.h>
#include <unistd.h>

#include "utils/error.hpp"
#include "utils/files.hpp"

using namespace wpc;
namespace fs = std::filesystem;

namespace {

/// A new empty folder, removed at the end.
struct TempFolder {
    fs::path path;
    TempFolder() {
        std::string pattern = (fs::temp_directory_path() / "wpc-files-XXXXXX").string();
        path = ::mkdtemp(pattern.data());
    }
    ~TempFolder() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string contents(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

}  // namespace

TEST_CASE("a file is read whole; a missing one is nothing, other problems are errors") {
    TempFolder temp;
    std::ofstream(temp.path / "a.toml") << "[led]\nmode = \"arc\"\n";
    CHECK(read_file(temp.path / "a.toml") == "[led]\nmode = \"arc\"\n");
    CHECK_FALSE(read_file(temp.path / "missing.toml"));
    CHECK_THROWS_AS(read_file(temp.path), Error);  // a folder
    CHECK_THROWS_WITH_AS(read_file(temp.path / "a.toml", 4), doctest::Contains("bigger than"), Error);

    // A FIFO would block the reader until something writes to it: refused at once instead.
    REQUIRE(::mkfifo((temp.path / "fifo.toml").c_str(), 0600) == 0);
    CHECK_THROWS_WITH_AS(read_file(temp.path / "fifo.toml"), doctest::Contains("not a regular file"), Error);
}

TEST_CASE("a file is replaced whole, keeping its permissions and the symlink to it") {
    TempFolder temp;
    fs::path real = temp.path / "dotfiles" / "config.toml";
    fs::create_directories(real.parent_path());
    std::ofstream(real) << "old";
    ::chmod(real.c_str(), 0600);
    fs::path link = temp.path / "config" / "config.toml";
    fs::create_directories(link.parent_path());
    fs::create_symlink(real, link);

    write_file_atomically(link, "new");
    CHECK(fs::is_symlink(link));  // still a link: dotfiles managers keep working
    CHECK(contents(real) == "new");
    struct stat info {};
    REQUIRE(::stat(real.c_str(), &info) == 0);
    CHECK((info.st_mode & 07777) == 0600);

    // A new file, in a folder that doesn't exist yet.
    fs::path fresh = temp.path / "made" / "here.toml";
    write_file_atomically(fresh, "x");
    CHECK(contents(fresh) == "x");
    // Nothing left behind: no temporary files.
    size_t files = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator(fresh.parent_path())) ++files;
    CHECK(files == 1);
}
