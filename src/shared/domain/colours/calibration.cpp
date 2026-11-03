#include "shared/domain/colours/calibration.hpp"

#include <algorithm>

#include "utils/text.hpp"

namespace wpc {

std::array<float, 3> Calibrations::gain(std::string_view device, std::string_view zone) const {
    for (const Calibration& c : entries) {
        bool zone_matches = !c.zone || contains_ignoring_case(zone, *c.zone);
        if (contains_ignoring_case(device, c.device) && zone_matches) {
            std::array<float, 3> out;
            for (int i = 0; i < 3; ++i) out[i] = static_cast<float>(std::clamp(c.gain[i], 0.0, 1.0));
            return out;
        }
    }
    return {1.0f, 1.0f, 1.0f};
}

}  // namespace wpc
