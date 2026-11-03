// Feature: the board's device, when there is a board. Opens the board if it isn't open.

#pragma once

#include <vector>

#include "drivers/fusion2/application/contracts/fusion_board.hpp"
#include "drivers/fusion2/application/models/board_state.hpp"

namespace wpc::fusion2 {

/// One device when the board is there, none otherwise. Throws wpc::Error.
std::vector<Device> list_devices(FusionBoard& board, const BoardState& state);

}  // namespace wpc::fusion2
