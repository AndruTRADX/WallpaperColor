# Razer Chroma keyboard (Huntsman Mini), through /dev/hidraw.
wpc_driver(razer SOURCES
  domain/settings.cpp domain/zones.cpp
  application/features/list_devices/list_devices.cpp application/features/show_frame/show_frame.cpp
  infrastructure/services/keyboard.cpp)
