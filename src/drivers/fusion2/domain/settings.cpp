#include "drivers/fusion2/domain/settings.hpp"

#include <algorithm>
#include <cmath>

#include "utils/text.hpp"

namespace wpc::fusion2 {

BoardSettings read_settings(const DriverSettings& table) {
    BoardSettings s;
    table.boolean("enabled");  // the old on/off switch, now [components]: read by the core
    if (std::optional<std::string> order = table.string("argb_order")) {
        std::string sorted = *order;
        std::sort(sorted.begin(), sorted.end());
        if (sorted != "BGR") {
            throw Error(cat("[", table.table(), "] argb_order: `", *order,
                            "` must have R, G and B in some order, like \"GRB\""));
        }
        s.argb_order = *order;
    }
    for (int h = 0; h < HEADERS; ++h) {
        std::string key = cat("argb_header", h + 1);
        std::optional<std::vector<double>> fans = table.numbers(key);
        if (!fans) continue;
        for (double leds : *fans) {
            if (leds < 0 || leds > 1024 || leds != std::floor(leds)) {
                throw Error(cat("[", table.table(), "] ", key, ": ", leds, " is not a number of LEDs (0-1024)"));
            }
            s.argb_headers[h].push_back(static_cast<uint32_t>(leds));
        }
    }
    return s;
}

}  // namespace wpc::fusion2
