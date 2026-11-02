# Libraries (one per Rust crate) and the two programs.
# Rule kept from the Rust version: the service never links the terminal UI, and `core` never
# talks to OpenRGB or D-Bus.

# Sockets, file descriptors, UTF-8 helpers.
add_library(wpc_util STATIC
  util/socket.cpp
  util/utf8.cpp
)
target_include_directories(wpc_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Colour types, extraction, config and the IPC protocol (wpc-core).
add_library(wpc_core STATIC
  core/color.cpp
  core/arc.cpp
  core/image.cpp
  core/extract.cpp
  core/config.cpp
  core/ipc.cpp
)
target_link_libraries(wpc_core
  PUBLIC wpc_util
  PRIVATE PkgConfig::WEBP tomlplusplus::tomlplusplus nlohmann_json::nlohmann_json)

# OpenRGB SDK client (wpc-openrgb).
add_library(wpc_openrgb STATIC openrgb/client.cpp)
target_link_libraries(wpc_openrgb PUBLIC wpc_core)

# The terminal client.
add_executable(wallpapercolor
  client/main.cpp
  client/tui.cpp
)
target_link_libraries(wallpapercolor PRIVATE wpc_core wpc_openrgb CLI11::CLI11)
