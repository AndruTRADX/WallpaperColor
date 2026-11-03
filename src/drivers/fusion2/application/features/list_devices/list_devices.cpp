#include "drivers/fusion2/application/features/list_devices/list_devices.hpp"

namespace wpc::fusion2 {

std::vector<Device> list_devices(FusionBoard& board, const BoardState& state) {
    if (!board.is_open() && !board.open(state.settings)) return {};  // no board is not an error
    return {state.layout.device};
}

}  // namespace wpc::fusion2
