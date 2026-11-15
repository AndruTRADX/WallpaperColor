#include "drivers/razer/keyboard.hpp"

#include <filesystem>
#include <optional>

#include "utils/error.hpp"
#include "utils/hidraw.hpp"
#include "utils/text.hpp"

namespace wpc::razer {

namespace {

/// The Huntsman Mini, through the USB interface that drives its lights.
constexpr hidraw::UsbId KNOWN_IDS[] = {{0x1532, 0x0257, 2}};

}  // namespace

bool HidKeyboard::open() {
    std::optional<std::filesystem::path> path = hidraw::find(KNOWN_IDS);
    if (!path) return false;
    try {
        fd_ = hidraw::open(*path, "install udev/70-wallpapercolor.rules");
    } catch (const Error& e) {
        throw Error(cat("Razer keyboard: ", e.what()));
    }
    return true;
}

bool HidKeyboard::still_connected() const {
    return hidraw::connected(fd_);
}

void HidKeyboard::send(const Report& report) {
    try {
        hidraw::set_feature(fd_, report);
    } catch (const Error& e) {
        throw Error(cat("Razer keyboard: ", e.what()));
    }
}

}  // namespace wpc::razer
