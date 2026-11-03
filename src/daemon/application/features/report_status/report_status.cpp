#include "daemon/application/features/report_status/report_status.hpp"

namespace wpc {

ipc::Status report_status(const ServiceState& state) {
    ipc::Status s = state.shown;
    s.version = WPC_VERSION;
    s.paused = state.paused;
    s.testing = state.testing;
    s.mode = state.config.led.mode;
    s.drivers.clear();
    for (const DriverModule& m : state.drivers.installed) {
        ipc::DriverState d{m.name, m.components, state.drivers.loaded.contains(m.name), {}};
        if (auto e = state.drivers.errors.find(m.name); e != state.drivers.errors.end()) d.error = e->second;
        s.drivers.push_back(std::move(d));
    }
    return s;
}

}  // namespace wpc
