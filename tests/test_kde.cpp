#include <doctest/doctest.h>

#include <fstream>

#include <unistd.h>

#include "kde/plasma.hpp"

using namespace wpc::kde;
namespace fs = std::filesystem;

TEST_CASE("urls") {
    CHECK(url_to_path("file:///a/b.png") == fs::path("/a/b.png"));
    CHECK(url_to_path("file:///a/b%20c.png") == fs::path("/a/b c.png"));
    CHECK(url_to_path("file:///w/%E5%AE%B6.jpg") == fs::path("/w/家.jpg"));
    CHECK(url_to_path("file:///w/家.jpg") == fs::path("/w/家.jpg"));
    CHECK(url_to_path("/plain/path.jpg") == fs::path("/plain/path.jpg"));
    CHECK(url_to_path("file:///a/100%") == fs::path("/a/100%"));
    CHECK_FALSE(url_to_path(""));
    CHECK_FALSE(url_to_path("null"));
    CHECK_FALSE(url_to_path("relative.png"));
}

TEST_CASE("scheme brightness") {
    CHECK(is_dark_scheme("[General]\nX=1\n[Colors:Window]\nBackgroundNormal=24,24,27\n"));
    CHECK_FALSE(is_dark_scheme("[Colors:Window]\nBackgroundNormal=239,240,241\n"));
    CHECK_FALSE(is_dark_scheme("[Colors:View]\nBackgroundNormal=0,0,0\n"));
    CHECK_FALSE(is_dark_scheme(""));
}

TEST_CASE("package resolution") {
    fs::path dir = fs::temp_directory_path() / ("wpc-kde-" + std::to_string(getpid()));
    struct File { const char* sub; const char* name; size_t size; };
    for (File f : {File{"images", "small.jpg", 10}, File{"images", "big.jpg", 100}, File{"images_dark", "dark.jpg", 50}}) {
        fs::path d = dir / "contents" / f.sub;
        fs::create_directories(d);
        std::ofstream(d / f.name) << std::string(f.size, '\0');
    }
    CHECK(resolve_image(dir, false)->filename() == "big.jpg");
    CHECK(resolve_image(dir, true)->filename() == "dark.jpg");
    fs::path file = dir / "contents/images/big.jpg";
    CHECK(resolve_image(file, true) == file);
    CHECK_FALSE(resolve_image(dir / "missing", false));
    fs::remove_all(dir);
}
