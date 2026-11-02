# WallpaperColor, C++ version. See docs/PLAN-CPP.md.
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.28)
project(WallpaperColor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)               # -std=c++23, not gnu++23
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)       # for clangd / editors
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
  -Wall -Wextra -Wpedantic
  # Colour maths must give the same bits as the Rust version: no fused multiply-add
  # (a*b+c in one instruction rounds differently) and never -ffast-math.
  -ffp-contract=off
)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)   # LTO, like the Rust release profile

# Version for --version and the IPC status, from project() above.
add_compile_definitions(WPC_VERSION="${PROJECT_VERSION}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)
find_package(tomlplusplus REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(CLI11 REQUIRED)
find_package(sdbus-c++ REQUIRED)
find_package(doctest REQUIRED)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
