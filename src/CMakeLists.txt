# Libraries (one per Rust crate) and the two programs.
# Rule kept from the Rust version: the service never links the terminal UI, and `core` never
# touches the network.

# Colour types, extraction, config and the IPC protocol (wpc-core).
add_library(wpc_core STATIC
  core/color.cpp
  core/arc.cpp
  core/image.cpp
  core/extract.cpp
  core/config.cpp
)
target_include_directories(wpc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wpc_core PRIVATE PkgConfig::WEBP tomlplusplus::tomlplusplus)

# The terminal client.
add_executable(wallpapercolor client/main.cpp)
target_link_libraries(wallpapercolor PRIVATE wpc_core CLI11::CLI11)
