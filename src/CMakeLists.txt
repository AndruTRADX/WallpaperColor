# Libraries and the two programs.
# The service never links the terminal UI, and `core` never talks to OpenRGB or D-Bus.

# Sockets, file descriptors, UTF-8 helpers.
add_library(wpc_util STATIC
  util/socket.cpp
  util/utf8.cpp
  util/inotify.cpp
)
target_include_directories(wpc_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Colour types, extraction, config and the IPC protocol.
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

# OpenRGB SDK client.
add_library(wpc_openrgb STATIC openrgb/client.cpp)
target_link_libraries(wpc_openrgb PUBLIC wpc_core)

# KDE Plasma wallpaper source over D-Bus.
add_library(wpc_kde STATIC kde/plasma.cpp)
target_link_libraries(wpc_kde PUBLIC wpc_core PRIVATE SDBusCpp::sdbus-c++)

# The service. No terminal UI code here.
add_executable(wallpapercolord daemon/main.cpp)
target_link_libraries(wallpapercolord PRIVATE wpc_core wpc_openrgb wpc_kde)

# The terminal client.
add_executable(wallpapercolor
  client/main.cpp
  client/settings.cpp
  client/tui.cpp
)
target_link_libraries(wallpapercolor PRIVATE wpc_core wpc_openrgb CLI11::CLI11
  ftxui::screen ftxui::dom ftxui::component)
