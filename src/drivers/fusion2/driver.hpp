// The fusion2 driver: the Gigabyte RGB Fusion 2 board, its onboard lights and its ARGB headers.
//
// FusionBoard is the port to the board's controller (board.hpp is the real one, over
// /dev/hidraw; the tests give a fake). BoardDriver is the Driver the programs see: it opens the
// board when needed (again after it was unplugged), closes it on a failure (the next devices()
// opens it again), and keeps each header's colours between frames.

#pragma once

#include <array>
#include <memory>
#include <span>
#include <vector>

#include "drivers/fusion2/protocol.hpp"
#include "drivers/sdk/driver.hpp"

namespace wpc::fusion2 {

/// What the driver needs from the board's RGB controller.
class FusionBoard {
public:
    virtual ~FusionBoard() = default;
    /// Finds the controller and opens it for `settings`; false if this computer has none.
    /// Throws wpc::Error if it is there but cannot be used (no permission).
    virtual bool open(const BoardSettings& settings) = 0;
    /// Closes it (opened again by the next open()).
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    /// False when the controller opened was unplugged since (plugged in again, it is another
    /// device: the open one only answers "no such device").
    virtual bool still_connected() const = 0;
    /// Shows `colour` on every single-colour zone. Throws wpc::Error.
    virtual void set_onboard(Rgb colour) = 0;
    /// Shows `colours` on the LEDs of ARGB header `header` (0 or 1), in order. Throws wpc::Error.
    virtual void set_header(int header, std::span<const Rgb> colours) = 0;
};

inline constexpr Component COMPONENTS[] = {Component::Motherboard, Component::Argb};

/// Every order of R, G and B; GRB (most fans) first, the default of BoardSettings.
inline constexpr const char* ARGB_ORDERS[] = {"GRB", "RGB", "RBG", "GBR", "BRG", "BGR"};
inline constexpr DriverOption OPTIONS[] = {
    {"argb_order", "ARGB order", ARGB_ORDERS, "change it if the ARGB fans show wrong colours"},
};

class BoardDriver final : public Driver {
public:
    /// `board`: the board's port (module.cpp gives the real one).
    BoardDriver(BoardSettings settings, std::unique_ptr<FusionBoard> board)
        : settings_(std::move(settings)), layout_(board_layout(settings_)), board_(std::move(board)) {}

    /// The board's device when there is a board, none otherwise. Throws wpc::Error.
    std::vector<Device> devices() override;
    void show(size_t device, std::span<const ZoneFrame> zones) override;

private:
    BoardSettings settings_;
    BoardLayout layout_;
    std::unique_ptr<FusionBoard> board_;
    /// What each header shows.
    std::array<std::vector<Rgb>, HEADERS> headers_;
};

}  // namespace wpc::fusion2
