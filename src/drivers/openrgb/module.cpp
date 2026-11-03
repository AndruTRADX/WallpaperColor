// What wpc-driver-openrgb.so exports, and the module's wiring: the driver gets the real
// OpenRGB client.

#include "drivers/openrgb/infrastructure/services/client.hpp"
#include "drivers/openrgb/presentation/driver.hpp"

namespace {

std::unique_ptr<wpc::Driver> create(const wpc::DriverSettings& table) {
    return std::make_unique<wpc::openrgb::ServerDriver>(wpc::openrgb::read_settings(table),
                                                        std::make_unique<wpc::openrgb::Client>());
}

const wpc::DriverInfo INFO = {
    .built_for = WPC_BUILD_ID,
    .name = "openrgb",
    .config_table = "openrgb",
    .components = wpc::openrgb::COMPONENTS,
    .create = create,
};

}  // namespace

extern "C" const wpc::DriverInfo* wpc_driver_info() {
    return &INFO;
}
