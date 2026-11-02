# WallpaperColor. See README.md and docs/ARCHITECTURE.md.
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build && ctest --test-dir build
# Release binaries that run on any distribution (see packaging/release.sh):
#   cmake -S . -B build-portable -G Ninja -DWPC_PORTABLE=ON
cmake_minimum_required(VERSION 3.28)
project(WallpaperColor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)               # -std=c++23, not gnu++23
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)       # for clangd / editors
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# FTXUI (terminal UI) is not packaged in the distribution: CMake downloads this exact version
# once into the build folder. Declared before our warning flags so they don't apply to it.
include(FetchContent)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v6.1.9
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ftxui)

# WPC_PORTABLE builds the binaries handed out on GitHub Releases: every library except glibc and
# libsystemd (present on any systemd distribution) is downloaded at a pinned version and linked
# statically, libstdc++ and libgcc included, so users run them without installing anything.
option(WPC_PORTABLE "Download and statically link the libraries (release binaries)" OFF)

if(WPC_PORTABLE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

  # libwebp: only the decoder library, none of its tools.
  foreach(opt ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO WEBPMUX EXTRAS LIBWEBPMUX
              WEBP_JS FUZZTEST)
    set(WEBP_BUILD_${opt} OFF CACHE BOOL "" FORCE)
  endforeach()
  FetchContent_Declare(libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG v1.6.0
    GIT_SHALLOW TRUE)

  set(SDBUSCPP_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
  set(SDBUSCPP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(SDBUSCPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SDBUSCPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDBUSCPP_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(sdbus-c++
    GIT_REPOSITORY https://github.com/Kistler-Group/sdbus-cpp.git
    GIT_TAG v2.3.1
    GIT_SHALLOW TRUE)

  # Header-only libraries.
  FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE)
  set(JSON_Install OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE)
  set(CLI11_PRECOMPILED OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.7.2
    GIT_SHALLOW TRUE)
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
    GIT_SHALLOW TRUE)

  FetchContent_MakeAvailable(libwebp sdbus-c++ tomlplusplus nlohmann_json CLI11 doctest)
  list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")   # include(doctest)

  add_library(wpc_webp INTERFACE)
  target_link_libraries(wpc_webp INTERFACE webpdecoder)
  target_include_directories(wpc_webp INTERFACE "${libwebp_SOURCE_DIR}/src")

  add_link_options(-static-libstdc++ -static-libgcc)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)   # link-time optimisation: smaller, faster

# Version for --version and the IPC status, from project() above.
add_compile_definitions(WPC_VERSION="${PROJECT_VERSION}")

# Otherwise the libraries installed on the system (README.md lists the packages).
if(NOT WPC_PORTABLE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)
  add_library(wpc_webp INTERFACE)
  target_link_libraries(wpc_webp INTERFACE PkgConfig::WEBP)
  find_package(tomlplusplus REQUIRED)
  find_package(nlohmann_json REQUIRED)
  find_package(CLI11 REQUIRED)
  find_package(sdbus-c++ REQUIRED)
  find_package(doctest REQUIRED)
endif()

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
