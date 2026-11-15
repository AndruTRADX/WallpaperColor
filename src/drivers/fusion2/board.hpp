// The real FusionBoard: the board's RGB controller through /dev/hidraw*, no other program
// needed. Your user needs access to it: udev/70-wallpapercolor.rules gives it to whoever is
// logged in. What the reports say: protocol.hpp.

#pragma once

#include <optional>
#include <span>

#include "drivers/fusion2/driver.hpp"
#include "drivers/fusion2/protocol.hpp"
#include "utils/fd.hpp"

namespace wpc::fusion2 {

class Board final : public FusionBoard {
public:
    /// Closed until open().
    Board() = default;

    /// Finds the controller, opens it and sets it up for direct mode; nullopt if this computer
    /// has none. Throws wpc::Error if it is there but cannot be used (no permission).
    static std::optional<Board> find(const BoardSettings& settings);

    bool open(const BoardSettings& settings) override;
    void close() override { *this = Board(); }
    bool is_open() const override { return fd_.valid(); }
    bool still_connected() const override;
    void set_onboard(Rgb colour) override;
    void set_header(int header, std::span<const Rgb> colours) override;

private:
    Board(Fd fd, const BoardSettings& settings, ColourPositions positions);
    void send(const Report& report);
    Report receive();
    /// LEDs on `header` (the sum of its fans).
    uint32_t header_leds(int header) const;

    Fd fd_;
    ColourPositions positions_{0, 1, 2};
    std::array<std::vector<uint32_t>, HEADERS> header_fans_;
};

}  // namespace wpc::fusion2
