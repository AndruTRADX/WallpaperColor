// What wpc-driver-razer.so exports: its DriverInfo, with the real keyboard service wired in.

#include "drivers/razer/infrastructure/services/keyboard.hpp"
#include "drivers/razer/presentation/driver.hpp"

namespace {

std::unique_ptr<wpc::Driver> create(const wpc::DriverSettings& table) {
    return std::make_unique<wpc::razer::KeyboardDriver>(wpc::razer::read_settings(table),
                                                        std::make_unique<wpc::razer::HidKeyboard>());
}

const wpc::DriverInfo INFO = {
    .built_for = WPC_BUILD_ID,
    .name = "razer",
    .config_table = "keyboard",
    .components = wpc::razer::COMPONENTS,
    .create = create,
    .options = wpc::razer::OPTIONS,
};

}  // namespace

extern "C" const wpc::DriverInfo* wpc_driver_info() {
    return &INFO;
}
