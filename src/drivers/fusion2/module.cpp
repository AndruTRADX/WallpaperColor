// What wpc-driver-fusion2.so exports: its DriverInfo, with the real board service wired in.

#include "drivers/fusion2/infrastructure/services/board.hpp"
#include "drivers/fusion2/presentation/driver.hpp"

namespace {

std::unique_ptr<wpc::Driver> create(const wpc::DriverSettings& table) {
    return std::make_unique<wpc::fusion2::BoardDriver>(wpc::fusion2::read_settings(table),
                                                       std::make_unique<wpc::fusion2::Board>());
}

const wpc::DriverInfo INFO = {
    .built_for = WPC_BUILD_ID,
    .name = "fusion2",
    .config_table = "motherboard",
    .components = wpc::fusion2::COMPONENTS,
    .create = create,
};

}  // namespace

extern "C" const wpc::DriverInfo* wpc_driver_info() {
    return &INFO;
}
