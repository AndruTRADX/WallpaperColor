// The fusion2 driver as the programs see it: the module's Driver (drivers/sdk). The Gigabyte RGB
// Fusion 2 board: its onboard lights and its ARGB headers. It only runs the module's features.

#pragma once

#include <memory>
#include <vector>

#include "drivers/fusion2/application/features/list_devices/list_devices.hpp"
#include "drivers/fusion2/application/features/show_frame/show_frame.hpp"
#include "drivers/sdk/driver.hpp"

namespace wpc::fusion2 {

inline constexpr Component COMPONENTS[] = {Component::Motherboard, Component::Argb};

/// Every order of R, G and B; GRB (most fans) first, the default of domain/settings.
inline constexpr const char* ARGB_ORDERS[] = {"GRB", "RGB", "RBG", "GBR", "BRG", "BGR"};
inline constexpr DriverOption OPTIONS[] = {
    {"argb_order", "ARGB order", ARGB_ORDERS, "change it if the ARGB fans show wrong colours"},
};

class BoardDriver final : public Driver {
public:
    /// `board`: the board's service (module.cpp gives the real one).
    BoardDriver(BoardSettings settings, std::unique_ptr<FusionBoard> board)
        : board_(std::move(board)), state_{settings, board_layout(settings), {}} {}

    std::vector<Device> devices() override { return list_devices(*board_, state_); }
    void show(size_t, std::span<const ZoneFrame> zones) override { show_frame(*board_, state_, zones); }

private:
    std::unique_ptr<FusionBoard> board_;
    BoardState state_;
};

}  // namespace wpc::fusion2
