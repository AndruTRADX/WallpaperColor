// The `[openrgb]` table of the config file, and its rules.

#pragma once

#include <string>

#include "drivers/sdk/driver_settings.hpp"

namespace wpc::openrgb {

struct ClientSettings {
    /// host:port. OpenLinkHub's built-in server listens on 6743 (enableOpenRGBTargetServer);
    /// OpenRGB itself on 6742.
    std::string address = "127.0.0.1:6743";
    /// Name shown to the server for this client.
    std::string client_name = "WallpaperColor";
};

/// Throws wpc::Error for a wrong type.
ClientSettings read_settings(const DriverSettings& table);

}  // namespace wpc::openrgb
