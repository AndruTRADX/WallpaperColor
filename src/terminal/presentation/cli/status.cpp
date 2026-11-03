// `status`, `pause`, `resume`, `apply`.

#include <cstdlib>
#include <format>

#include "terminal/application/features/control_service/control_service.hpp"
#include "terminal/application/features/service_status/service_status.hpp"
#include "terminal/presentation/cli/commands.hpp"
#include "terminal/presentation/cli/output.hpp"

namespace wpc {

int run_status(TerminalParts& parts) {
    ServiceStatus status = service_status(parts.service, parts.codec);
    const ipc::Status& s = status.status;
    out(std::format("{} v{}  mode {}", s.paused ? "paused" : "running", s.version, mode_name(s.mode)));
    out(std::format("wallpaper  {} ({})", s.image ? s.image->string() : "—", s.plugin));
    std::vector<std::string> stops;
    for (Rgb c : status.stops) stops.push_back(swatch(c, 2) + c.hex());
    out("colours    " + join(stops, " "));
    for (const ipc::DeviceInfo& d : s.devices) {
        std::vector<std::string> zones;
        for (const ipc::ZoneInfo& z : d.zones) zones.push_back(std::format("{} ({})", z.name, z.leds));
        out(std::format("device     {}: {}", d.name, join(zones, ", ")));
    }
    if (s.error) out("problem    " + *s.error);
    return EXIT_SUCCESS;
}

int run_request(TerminalParts& parts, ipc::Request request, std::string_view done) {
    control_service(parts.service, parts.codec, request);
    out(std::string(done));
    return EXIT_SUCCESS;
}

}  // namespace wpc
