# Gigabyte RGB Fusion 2 motherboard RGB, through /dev/hidraw.
wpc_driver(fusion2 SOURCES
  domain/settings.cpp domain/zones.cpp
  application/features/list_devices/list_devices.cpp application/features/show_frame/show_frame.cpp
  infrastructure/services/board.cpp)
