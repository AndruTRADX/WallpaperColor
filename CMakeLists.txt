# WallpaperColor. See README.md and docs/ARCHITECTURE.md.
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build && ctest --test-dir build
# Release binaries that run on any distribution (see packaging/release.sh):
#   cmake -S . -B build-portable -G Ninja -DWPC_PORTABLE=ON
cmake_minimum_required(VERSION 3.28)
project(WallpaperColor VERSION 0.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)               # -std=c++23, not gnu++23
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)       # for clangd / editors
set(CMAKE_POSITION_INDEPENDENT_CODE ON)     # driver modules link wpc_util into a shared object
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

# WPC_PORTABLE builds the binaries handed out on GitHub Releases: every library except glibc,
# libstdc++, libsystemd (present on any systemd distribution) and zlib is downloaded at a pinned
# version and linked statically, so users run them without installing anything. libstdc++ and
# zlib are on every KDE system (Plasma is C++, Qt uses zlib). libstdc++ stays shared because
# the programs and the driver modules must use the same one.
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

  # libjpeg-turbo: only the static libjpeg API, no tools, no TurboJPEG. It refuses to be
  # built as part of another project, so it is built on its own and installed into the build
  # folder. Its SIMD code needs nasm at build time; without it, it still builds, just slower.
  include(ExternalProject)
  set(JPEG_PREFIX "${CMAKE_BINARY_DIR}/libjpeg-turbo")
  ExternalProject_Add(libjpeg-turbo
    GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
    GIT_TAG 3.2.1
    GIT_SHALLOW TRUE
    INSTALL_DIR "${JPEG_PREFIX}"
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_INSTALL_LIBDIR=lib
               -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
               -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF
               -DWITH_TOOLS=OFF -DWITH_TESTS=OFF
    BUILD_BYPRODUCTS "${JPEG_PREFIX}/lib/libjpeg.a")
  file(MAKE_DIRECTORY "${JPEG_PREFIX}/include")
  add_library(wpc_jpeg STATIC IMPORTED)
  set_target_properties(wpc_jpeg PROPERTIES
    IMPORTED_LOCATION "${JPEG_PREFIX}/lib/libjpeg.a"
    INTERFACE_INCLUDE_DIRECTORIES "${JPEG_PREFIX}/include")
  add_dependencies(wpc_jpeg libjpeg-turbo)

  # libpng: the static library only. It uses the system's zlib (every KDE system has it).
  set(PNG_SHARED OFF CACHE BOOL "" FORCE)
  set(PNG_STATIC ON CACHE BOOL "" FORCE)
  set(PNG_TESTS OFF CACHE BOOL "" FORCE)
  set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
  set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
  set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(libpng
    GIT_REPOSITORY https://github.com/pnggroup/libpng.git
    GIT_TAG v1.6.59
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

  FetchContent_MakeAvailable(libwebp libpng sdbus-c++ tomlplusplus nlohmann_json CLI11 doctest)
  list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")   # include(doctest)

  add_library(wpc_webp INTERFACE)
  target_link_libraries(wpc_webp INTERFACE webpdecoder)
  target_include_directories(wpc_webp INTERFACE "${libwebp_SOURCE_DIR}/src")
  add_library(wpc_png INTERFACE)
  target_link_libraries(wpc_png INTERFACE png_static)
  target_include_directories(wpc_png INTERFACE "${libpng_SOURCE_DIR}" "${libpng_BINARY_DIR}")

endif()

add_compile_options(-Wall -Wextra -Wpedantic)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)   # link-time optimisation: smaller, faster

# Version for --version and the IPC status, from project() above.
add_compile_definitions(WPC_VERSION="${PROJECT_VERSION}")

# Driver modules and programs must come from the same build: each module carries this and the
# programs refuse any other. It changes with the version, the compiler and the headers modules
# and programs share, so a stale module is refused instead of crashing.
set(WPC_DRIVER_API_FILES
  src/drivers/sdk/driver.hpp src/drivers/sdk/hardware.hpp src/drivers/sdk/driver_settings.hpp src/shared/domain/colours/color.hpp)
set(WPC_DRIVER_API_TEXT "")
foreach(f ${WPC_DRIVER_API_FILES})
  file(READ "${PROJECT_SOURCE_DIR}/${f}" text)
  string(APPEND WPC_DRIVER_API_TEXT "${text}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/${f}")
endforeach()
string(MD5 WPC_DRIVER_API_HASH "${WPC_DRIVER_API_TEXT}")
string(SUBSTRING "${WPC_DRIVER_API_HASH}" 0 8 WPC_DRIVER_API_HASH)
add_compile_definitions(
  WPC_BUILD_ID="${PROJECT_VERSION} ${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION} ${WPC_DRIVER_API_HASH}")
include(cmake/drivers.cmake)

# Otherwise the libraries installed on the system (README.md lists the packages).
if(NOT WPC_PORTABLE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)
  add_library(wpc_webp INTERFACE)
  target_link_libraries(wpc_webp INTERFACE PkgConfig::WEBP)
  pkg_check_modules(JPEG REQUIRED IMPORTED_TARGET libjpeg)   # libjpeg-turbo
  add_library(wpc_jpeg INTERFACE)
  target_link_libraries(wpc_jpeg INTERFACE PkgConfig::JPEG)
  pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng16)
  add_library(wpc_png INTERFACE)
  target_link_libraries(wpc_png INTERFACE PkgConfig::PNG)
  find_package(tomlplusplus REQUIRED)
  find_package(nlohmann_json REQUIRED)
  find_package(CLI11 REQUIRED)
  find_package(sdbus-c++ REQUIRED)
  find_package(doctest REQUIRED)
endif()

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
