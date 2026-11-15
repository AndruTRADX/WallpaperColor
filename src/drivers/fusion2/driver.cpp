#include "drivers/fusion2/driver.hpp"

#include "utils/error.hpp"

namespace wpc::fusion2 {

std::vector<Device> BoardDriver::devices() {
    if (board_->is_open() && !board_->still_connected()) board_->close();  // unplugged since
    if (!board_->is_open() && !board_->open(settings_)) return {};          // no board is not an error
    return {layout_.device};
}

void BoardDriver::show(size_t, std::span<const ZoneFrame> zones) {
    if (!board_->is_open()) throw Error("motherboard RGB: not open");
    try {
        BoardFrame frame = place_frame(layout_, zones, headers_);
        if (frame.onboard) board_->set_onboard(*frame.onboard);
        for (int h = 0; h < HEADERS; ++h) {
            if (frame.header_changed[h]) board_->set_header(h, headers_[h]);
        }
    } catch (const Error&) {
        board_->close();  // opened again by the next devices()
        throw;
    }
}

}  // namespace wpc::fusion2
