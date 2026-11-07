# Everything is grouped by who uses it (docs/ARCHITECTURE.md):
#
#   daemon/     wallpapercolord, in layers: domain, application, infrastructure, presentation
#   terminal/   wallpapercolor, in layers: application, infrastructure, presentation
#   drivers/    sdk (what a driver is written against), host (loading one), one folder each
#   shared/     what both programs use, in layers: domain, models, infrastructure/services
#   utils/      small pieces of plain code (sockets, inotify, TOML text…)
#
# Each piece is a library that links only what it may use; cmake/check_layers.py checks the
# #includes and cmake/usage_map.py checks that every file is where its users are.

# ---------------------------------------------------------------------------------------------
# Used by several parts

file(GLOB WPC_UTILS CONFIGURE_DEPENDS utils/*.cpp)
list(REMOVE_ITEM WPC_UTILS ${CMAKE_CURRENT_SOURCE_DIR}/utils/picture_decoding.cpp)   # a module, below
add_library(wpc_utils STATIC ${WPC_UTILS})
target_include_directories(wpc_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wpc_utils PRIVATE wpc_toml)

# The picture decoders and the image libraries, as their own file, wpc-pictures.so, next to the
# programs: picture_files loads it only while it reads a picture, so they take no memory the
# rest of the time. Built like a driver module (cmake/drivers.cmake), and optimised for speed
# (-O3, not the -Os of the rest): it is only in memory while it works.
add_library(wpc-pictures MODULE utils/picture_decoding.cpp)
target_include_directories(wpc-pictures PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wpc-pictures PRIVATE wpc_jpeg wpc_png wpc_webp)
target_compile_options(wpc-pictures PRIVATE -fno-gnu-unique $<$<CONFIG:Release>:-O3>)
target_link_options(wpc-pictures PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined)
set_target_properties(wpc-pictures PROPERTIES
  PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# What a driver module is written against (header-only).
add_library(wpc_drivers_sdk INTERFACE)
target_link_libraries(wpc_drivers_sdk INTERFACE wpc_utils)

# The shared entities and their rules, and the messages' structure.
file(GLOB_RECURSE WPC_SHARED_DOMAIN CONFIGURE_DEPENDS shared/domain/*.cpp)
add_library(wpc_shared_domain STATIC ${WPC_SHARED_DOMAIN})
target_link_libraries(wpc_shared_domain PUBLIC wpc_drivers_sdk)
# The colour maths runs over every pixel kept from a picture: -O2, not -Os (8 % faster, 15 KB).
target_compile_options(wpc_shared_domain PRIVATE $<$<CONFIG:Release>:-O2>)

# The shared services, one library each: a program links only those it uses.
add_library(wpc_shared_settings_file STATIC shared/infrastructure/services/settings_file.cpp)
target_link_libraries(wpc_shared_settings_file PUBLIC wpc_shared_domain PRIVATE wpc_toml)

add_library(wpc_shared_picture_files STATIC shared/infrastructure/services/picture_files.cpp)
target_link_libraries(wpc_shared_picture_files PUBLIC wpc_shared_domain PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(wpc_shared_picture_files wpc-pictures)   # a program that reads pictures needs it

add_library(wpc_shared_protocol STATIC
  shared/infrastructure/services/protocol_json.cpp shared/infrastructure/services/socket_path.cpp)
target_link_libraries(wpc_shared_protocol PUBLIC wpc_shared_domain PRIVATE nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------------------------------------
# Drivers: how programs load them, and each driver as its own module (cmake/drivers.cmake)

add_library(wpc_drivers_host STATIC drivers/host/services/module_catalog.cpp)
target_link_libraries(wpc_drivers_host PUBLIC wpc_drivers_sdk PRIVATE ${CMAKE_DL_LIBS})

add_subdirectory(drivers/openrgb)
add_subdirectory(drivers/fusion2)
add_subdirectory(drivers/razer)
get_property(WPC_DRIVER_MODULES GLOBAL PROPERTY WPC_DRIVER_MODULES)
add_custom_target(wpc_drivers ALL DEPENDS ${WPC_DRIVER_MODULES})

# ---------------------------------------------------------------------------------------------
# The service

# Its rules, features, events and presentation: they only know its contracts.
file(GLOB_RECURSE WPC_DAEMON_CORE CONFIGURE_DEPENDS
  daemon/domain/*.cpp daemon/application/*.cpp daemon/presentation/*.cpp)
add_library(wpc_daemon_core STATIC ${WPC_DAEMON_CORE})
target_link_libraries(wpc_daemon_core PUBLIC wpc_shared_domain wpc_drivers_sdk)

# What implements its contracts.
file(GLOB_RECURSE WPC_DAEMON_INFRASTRUCTURE CONFIGURE_DEPENDS daemon/infrastructure/*.cpp)
add_library(wpc_daemon_infrastructure STATIC ${WPC_DAEMON_INFRASTRUCTURE})
target_link_libraries(wpc_daemon_infrastructure
  PUBLIC wpc_daemon_core wpc_drivers_host wpc_shared_settings_file wpc_shared_picture_files wpc_shared_protocol
  PRIVATE SDBusCpp::sdbus-c++)

add_executable(wallpapercolord daemon/main.cpp)
target_link_libraries(wallpapercolord PRIVATE wpc_daemon_infrastructure)

# ---------------------------------------------------------------------------------------------
# The terminal

# Its features: they only know its contracts.
file(GLOB_RECURSE WPC_TERMINAL_CORE CONFIGURE_DEPENDS terminal/application/*.cpp)
add_library(wpc_terminal_core STATIC ${WPC_TERMINAL_CORE})
target_link_libraries(wpc_terminal_core PUBLIC wpc_shared_domain wpc_drivers_sdk)

# What implements its contracts.
file(GLOB_RECURSE WPC_TERMINAL_INFRASTRUCTURE CONFIGURE_DEPENDS terminal/infrastructure/*.cpp)
add_library(wpc_terminal_infrastructure STATIC ${WPC_TERMINAL_INFRASTRUCTURE})
target_link_libraries(wpc_terminal_infrastructure
  PUBLIC wpc_terminal_core wpc_drivers_host wpc_shared_settings_file wpc_shared_picture_files wpc_shared_protocol)

# The commands and the terminal UI.
file(GLOB_RECURSE WPC_TERMINAL_UI CONFIGURE_DEPENDS terminal/presentation/*.cpp)
add_library(wpc_terminal_ui STATIC ${WPC_TERMINAL_UI})
target_link_libraries(wpc_terminal_ui PUBLIC wpc_terminal_core PRIVATE ftxui::screen ftxui::dom ftxui::component)

add_executable(wallpapercolor terminal/main.cpp)
target_link_libraries(wallpapercolor PRIVATE wpc_terminal_ui wpc_terminal_infrastructure CLI11::CLI11)
