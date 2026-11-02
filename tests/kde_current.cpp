// Prints the current wallpaper of the first screen (the Rust example `current`).
#include <print>

#include "kde/plasma.hpp"

int main() {
    using namespace wpc::kde;
    Plasma plasma = Plasma::connect();
    Wallpaper w = plasma.wallpaper(0);
    std::println("plugin: {}", w.plugin);
    std::println("image: {}", w.image ? w.image->string() : "none");
    for (const auto& p : w.slide_paths) std::println("slide path: {}", p.string());
    if (w.image) {
        auto resolved = resolve_image(*w.image, dark_mode());
        std::println("resolved: {}", resolved ? resolved->string() : "none");
    }
}
