# Devices of an OpenRGB SDK server (OpenLinkHub's built-in one, or OpenRGB).
wpc_driver(openrgb SOURCES
  domain/zones.cpp domain/settings.cpp
  application/features/list_devices/list_devices.cpp application/features/show_frame/show_frame.cpp
  infrastructure/services/client.cpp)
