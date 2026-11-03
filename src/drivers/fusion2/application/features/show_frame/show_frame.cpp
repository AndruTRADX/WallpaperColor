#include "drivers/fusion2/application/features/show_frame/show_frame.hpp"

#include "utils/error.hpp"

namespace wpc::fusion2 {

void show_frame(FusionBoard& board, BoardState& state, std::span<const ZoneFrame> zones) {
    if (!board.is_open()) throw Error("motherboard RGB: not open");
    try {
        BoardFrame frame = place_frame(state.layout, zones, state.headers);
        if (frame.onboard) board.set_onboard(*frame.onboard);
        for (int h = 0; h < HEADERS; ++h) {
            if (frame.header_changed[h]) board.set_header(h, state.headers[h]);
        }
    } catch (const Error&) {
        board.close();  // opened again by the next list_devices()
        throw;
    }
}

}  // namespace wpc::fusion2
