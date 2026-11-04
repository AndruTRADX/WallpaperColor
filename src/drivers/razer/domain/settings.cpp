#include "drivers/razer/domain/settings.hpp"

#include "utils/text.hpp"

namespace wpc::razer {

KeyboardSettings read_settings(const DriverSettings& table) {
    KeyboardSettings s;
    if (std::optional<std::string> direction = table.string("direction")) {
        if (*direction == "across") {
            s.direction = Direction::Across;
        } else if (*direction == "down") {
            s.direction = Direction::Down;
        } else {
            throw Error(cat("[", table.table(), "] direction: `", *direction, "` must be \"across\" or \"down\""));
        }
    }
    return s;
}

}  // namespace wpc::razer
