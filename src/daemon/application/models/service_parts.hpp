// Model: the parts the service's features run with, one implementation of each contract, as
// main.cpp wires them. The service loop hands each feature only the parts it uses.

#pragma once

#include "daemon/application/contracts/clock.hpp"
#include "daemon/application/contracts/colour_scheme.hpp"
#include "daemon/application/contracts/desktop_wallpaper.hpp"
#include "daemon/application/contracts/external/driver_modules.hpp"
#include "daemon/application/contracts/external/message_codec.hpp"
#include "daemon/application/contracts/external/pictures.hpp"
#include "daemon/application/contracts/external/settings_source.hpp"
#include "daemon/application/contracts/memory.hpp"
#include "daemon/application/contracts/wallpaper_packages.hpp"

namespace wpc {

struct ServiceParts {
    DesktopWallpaper& desktop;
    WallpaperPackages& packages;
    ColourScheme& scheme;
    Pictures& pictures;
    SettingsSource& settings;
    DriverModules& drivers;
    MessageCodec& codec;
    Clock& clock;
    Memory& memory;
};

}  // namespace wpc
