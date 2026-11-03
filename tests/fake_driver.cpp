// A driver module for the tests (wpc-driver-fake.so): one device whose zones come from its
// settings. show() throws a wpc::Error describing the frame, so a test can see what arrived
// and that exceptions cross from a module into the program.

#include <format>

#include "drivers/sdk/driver.hpp"

namespace {

class FakeDriver final : public wpc::Driver {
public:
    explicit FakeDriver(const wpc::DriverSettings& settings) {
        for (double leds : settings.numbers("zones").value_or(std::vector<double>{3})) {
            zones_.push_back(wpc::Zone{"Zone", static_cast<uint32_t>(leds), wpc::Component::Strip, wpc::Shape::Line});
        }
    }

    std::vector<wpc::Device> devices() override { return {wpc::Device{"fake", "Fake device", zones_, {}}}; }

    void show(size_t device, std::span<const wpc::ZoneFrame> zones) override {
        std::string seen = std::format("device {}:", device);
        for (const wpc::ZoneFrame& z : zones) seen += std::format(" zone {} {}", z.zone, z.colours[0].r);
        throw wpc::Error(seen);
    }

private:
    std::vector<wpc::Zone> zones_;
};

constexpr wpc::Component COMPONENTS[] = {wpc::Component::Strip};

const wpc::DriverInfo INFO = {
#ifdef FAKE_FROM_ANOTHER_BUILD
    .built_for = "0.0.0 another build",
    .name = "fake-other",
#else
    .built_for = WPC_BUILD_ID,
    .name = "fake",
#endif
    .config_table = "fake",
    .components = COMPONENTS,
    .create = [](const wpc::DriverSettings& s) -> std::unique_ptr<wpc::Driver> { return std::make_unique<FakeDriver>(s); },
};

}  // namespace

extern "C" const wpc::DriverInfo* wpc_driver_info() {
    return &INFO;
}
