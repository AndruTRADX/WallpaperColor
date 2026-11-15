# The modules, from the bottom up (docs/ARCHITECTURE.md). Each is a library that links only
# the modules it may use, so a forbidden dependency doesn't link; cmake/check_dependencies.py
# also checks every #include (ctest).
#
#   utils/          Linux and C++ building blocks; nothing about WallpaperColor
#   drivers/sdk/    what a driver module is written against: plain types and the Driver port
#   colours/        colour maths, resizing pictures, picking colours, painting (pure)
#   settings/       the settings and the config file
#   protocol/       the messages between the terminal and the service
#   pictures/       reading pictures; the decoders are a module of their own (wpc-pictures.so)
#   drivers/host/   loading driver modules
#   drivers/<name>/ one module per kind of hardware (wpc-driver-<name>.so)
#   daemon/         wallpapercolord: service/ (its logic), kde/ and system/ (its ports)
#   terminal/       wallpapercolor: app/ (its use cases), system/, cli/, tui/

# ---------------------------------------------------------------------------------------------
# Used by everything

file(GLOB WPC_UTILS CONFIGURE_DEPENDS utils/*.cpp)
add_library(wpc_utils STATIC ${WPC_UTILS})
target_include_directories(wpc_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wpc_utils PRIVATE wpc_toml ${CMAKE_DL_LIBS})

# What a driver module is written against (header-only).
add_library(wpc_sdk INTERFACE)
target_link_libraries(wpc_sdk INTERFACE wpc_utils)

# The colour maths runs over every pixel kept from a picture: -O2, not -Os (8 % faster, 15 KB).
file(GLOB WPC_COLOURS CONFIGURE_DEPENDS colours/*.cpp)
add_library(wpc_colours STATIC ${WPC_COLOURS})
target_link_libraries(wpc_colours PUBLIC wpc_sdk)
target_compile_options(wpc_colours PRIVATE $<$<CONFIG:Release>:-O2>)

add_library(wpc_settings STATIC settings/settings.cpp settings/settings_file.cpp)
target_link_libraries(wpc_settings PUBLIC wpc_colours PRIVATE wpc_toml)

add_library(wpc_protocol STATIC protocol/json.cpp protocol/socket_path.cpp)
target_link_libraries(wpc_protocol PUBLIC wpc_colours PRIVATE nlohmann_json::nlohmann_json)

# The picture decoders and the image libraries, as their own file, wpc-pictures.so, next to the
# programs: picture_files loads it only while it reads a picture, so they take no memory the
# rest of the time. Built like a driver module (cmake/drivers.cmake), and optimised for speed
# (-O3, not the -Os of the rest): it is only in memory while it works.
add_library(wpc-pictures MODULE pictures/decoders.cpp)
target_include_directories(wpc-pictures PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wpc-pictures PRIVATE wpc_jpeg wpc_png wpc_webp)
target_compile_options(wpc-pictures PRIVATE -fno-gnu-unique $<$<CONFIG:Release>:-O3>)
target_link_options(wpc-pictures PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined)
set_target_properties(wpc-pictures PROPERTIES
  PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

add_library(wpc_pictures STATIC pictures/picture_files.cpp)
target_link_libraries(wpc_pictures PUBLIC wpc_colours)
add_dependencies(wpc_pictures wpc-pictures)   # a program that reads pictures needs it

# ---------------------------------------------------------------------------------------------
# Drivers: how programs load them, and each driver as its own module (cmake/drivers.cmake)

add_library(wpc_driver_host STATIC drivers/host/driver_host.cpp)
target_link_libraries(wpc_driver_host PUBLIC wpc_sdk)

add_subdirectory(drivers/openrgb)
add_subdirectory(drivers/fusion2)
add_subdirectory(drivers/razer)
get_property(WPC_DRIVER_MODULES GLOBAL PROPERTY WPC_DRIVER_MODULES)
add_custom_target(wpc_drivers ALL DEPENDS ${WPC_DRIVER_MODULES})

# ---------------------------------------------------------------------------------------------
# The service

# Its logic: it only knows its ports (no D-Bus, no sockets, no inotify here).
file(GLOB WPC_DAEMON_SERVICE CONFIGURE_DEPENDS daemon/service/*.cpp)
add_library(wpc_daemon_service STATIC ${WPC_DAEMON_SERVICE})
target_link_libraries(wpc_daemon_service PUBLIC wpc_settings wpc_protocol wpc_pictures wpc_driver_host)

# Its ports' implementations: the KDE desktop and the rest of the computer.
file(GLOB WPC_DAEMON_SYSTEM CONFIGURE_DEPENDS daemon/kde/*.cpp daemon/system/*.cpp)
add_library(wpc_daemon_system STATIC ${WPC_DAEMON_SYSTEM})
target_link_libraries(wpc_daemon_system PUBLIC wpc_daemon_service PRIVATE SDBusCpp::sdbus-c++)

add_executable(wallpapercolord daemon/main.cpp)
target_link_libraries(wallpapercolord PRIVATE wpc_daemon_system)

# ---------------------------------------------------------------------------------------------
# The terminal

# Its use cases: they only know its ports.
file(GLOB WPC_TERMINAL_APP CONFIGURE_DEPENDS terminal/app/*.cpp)
add_library(wpc_terminal_app STATIC ${WPC_TERMINAL_APP})
target_link_libraries(wpc_terminal_app PUBLIC wpc_settings wpc_protocol wpc_pictures wpc_driver_host)

# Its ports' implementations: the settings editor and the service's socket.
file(GLOB WPC_TERMINAL_SYSTEM CONFIGURE_DEPENDS terminal/system/*.cpp)
add_library(wpc_terminal_system STATIC ${WPC_TERMINAL_SYSTEM})
target_link_libraries(wpc_terminal_system PUBLIC wpc_terminal_app)

# The commands and the terminal UI.
file(GLOB WPC_TERMINAL_UI CONFIGURE_DEPENDS terminal/cli/*.cpp terminal/tui/*.cpp)
add_library(wpc_terminal_ui STATIC ${WPC_TERMINAL_UI})
target_link_libraries(wpc_terminal_ui PUBLIC wpc_terminal_app PRIVATE ftxui::screen ftxui::dom ftxui::component)

add_executable(wallpapercolor terminal/main.cpp)
target_link_libraries(wallpapercolor PRIVATE wpc_terminal_ui wpc_terminal_system CLI11::CLI11)
