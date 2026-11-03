// Feature: one frame on the board: the onboard colour and each header whose zones are in it.
// A failure closes the board: the next list_devices() opens it again.

#pragma once

#include <span>

#include "drivers/fusion2/application/contracts/fusion_board.hpp"
#include "drivers/fusion2/application/models/board_state.hpp"

namespace wpc::fusion2 {

/// Throws wpc::Error.
void show_frame(FusionBoard& board, BoardState& state, std::span<const ZoneFrame> zones);

}  // namespace wpc::fusion2
